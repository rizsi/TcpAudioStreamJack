/* 
 * Open a TCP server and create a jack connection for each incoming TCP connection
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <signal.h>

#include <speex/speex_resampler.h>
#include "speex/speex_preprocess.h"
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include "tcp-protocol.h"
#include "linked_list.h"
#include "ringBuffer.h"

/// Epoll events list size that is maximum to process at once. Program is intended to serve 1 client so 32 is way too much but costs nothing.
#define MAX_EVENTS      32

/// Each connected client stores one of this structure
typedef struct tcpClient_str{
	/// connected clients are organized into a linked list
	linked_list list;
    int fd;
    char name[256];
    volatile bool started;
    /// Buffer to store data received from the TCP socket without modification. This stream contains messages prefixed with struct chunk_header
    /// The messages are parsed by process_messages()
    ringBuffer_t rb;
    /// Buffer to store audio frames converted to local samplerate. This is source of playback through Jack
    ringBuffer_t audio;
    /// Buffer to store audio frames in the samplerate of the client how it was sent.
    /// Written by process_messages() and read by resample(). After resampling the new audio samples are written into the "audio" buffer.
    ringBuffer_t audioOriginal;
    /// Jack port identifiers onto which this client audio is written to
    jack_port_t * ports[NPORT];
    /// Sample rate of the client source. Set by the R_MSG_STREAM_PARAMETERS message that has to arrive before the first audio frame.
    uint32_t samplerate;
    /// Count the samples written into the audio stream. Just for debugging purpose.
    uint32_t countSamples;
    /// Resampler that does resampling of input audio data from tcpClient.samplerate to local samplerate.
    /// When the "audio" buffer is too long or too short then playback speed is corrected with 1-3% by changing the input samplerate.
	SpeexResamplerState * resampler_state;
} tcpClient;

/// This object is stored into the epoll event.ptr field. used to check whether the event originates from the listening server port.
typedef struct {
    int fd;
} tcpServer;

/// Lock to protect linked list of tcpClients iteration and modification. Real time Jack thread locks all iteration. Others lock just add and delete which is always fast hopefully
static pthread_mutex_t tcpClients_list_lock = PTHREAD_MUTEX_INITIALIZER;
/// Linked list of all connected clients
static linked_list * tcpClients;

/// epoll fd - a single epoll instance is used to handle all networking
static int epfd;
/// Jack API client - a single instance is used for the whole lifecycle of the server
static jack_client_t *jackClient;
/// Signal that exit was requested by user (ctrl-c)
static volatile bool exitProgram=false;
/// local samplerate of the jackClient Jack connection.
uint32_t samplerate;
/// Connect the output to these ports when the output ports are created.
static char port_target_names[][128]={"Built-in Audio Analog Stereo:playback_FL", "Built-in Audio Analog Stereo:playback_FR"};

/// register events of fd to epfd
static void epoll_ctl_add(int epfd, int fd, uint32_t events, void * ptr)
{
	struct epoll_event ev;
	ev.events = events;
	ev.data.ptr = ptr;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		perror("epoll_ctl()\n");
		exit(1);
	}
}

/// Fill the sockaddr structure with server bind address
static void set_sockaddr(struct sockaddr_in *addr, int port)
{
	bzero((char *)addr, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = INADDR_ANY;
	addr->sin_port = htons(port);
}

/// Set up the fd to be handled non-blocking
static int setnonblocking(int sockfd)
{
	if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) ==
	    -1) {
		assert(false);
		return -1;
	}
	return 0;
}
/// Jack calls us back for each requested frame for all ports handled by this program
int jack_process_frames_callback (jack_nframes_t nframes, void *arg)
{
	pthread_mutex_lock (&tcpClients_list_lock);
	linked_list * curr=tcpClients;
	while(curr!=NULL)
	{
		tcpClient * c=(tcpClient *) curr;
		if(c->started)
		{
			jack_default_audio_sample_t * buff[NPORT];
			for(int i=0;i<NPORT;++i)
			{
				buff[i]=jack_port_get_buffer(c->ports[i], nframes);
			}
			for(int j=0;j<nframes;++j)
			{
				for(int i=0;i<NPORT;++i)
				{
					ringBuffer_read(&c->audio, SAMPLE_SIZE_BYTES, (uint8_t *)(buff[i]+j));
				}
			}
		}
		curr=curr->next;
	}
	pthread_mutex_unlock(&tcpClients_list_lock);
	return 0;
}
/// Shut down a client and free all resources that was allocated for the client.
/// Also remove the tcpClient struct from the linked list and free the client structure itself.
static void client_shutdown (tcpClient * tcp)
{
	printf("client_shutdown ...\n");
	assert(tcp!=NULL);
	epoll_ctl(epfd, EPOLL_CTL_DEL, tcp->fd, NULL);
	close(tcp->fd);
	for (int i = 0; i < NPORT; i++) {
		jack_port_unregister(jackClient, tcp->ports[i]);
	}
	pthread_mutex_lock(&tcpClients_list_lock);
	linked_list_remove(&tcpClients, &(tcp->list));
	pthread_mutex_unlock(&tcpClients_list_lock);
	if(tcp->rb.buffer!=NULL)
	{
		free(tcp->rb.buffer);
		tcp->rb.buffer=NULL;
	}
	if(tcp->audio.buffer!=NULL)
	{
		free(tcp->audio.buffer);
		tcp->audio.buffer=NULL;
	}
	if(tcp->audioOriginal.buffer!=NULL)
	{
		free(tcp->audioOriginal.buffer);
		tcp->audioOriginal.buffer=NULL;
	}
	printf("client_shutdown done %s\n", tcp->name);
	free(tcp);
}
/// Jack shutdown callback - with pipewire it is never called in my experience
/// When Jack shutdown happens there is nothing to do but exit the program.
static void jack_shutdown (void * arg)
{
	printf("jack_shutdown\n");
	exit(0);
}
/// Create a client object by tcp client socked fd
/// Initialize all fields and add the client to the tcpClients list and to the epoll structure.
/// Also open output Jack ports and connect their output to the desired port (port_target_names)
static void openClient(int fd, struct sockaddr_in * cli_addr)
{
	char buf[128];
	tcpClient * tcp = (tcpClient *)calloc(sizeof(tcpClient), 1);
	assert(tcp!=NULL);
	tcp->fd=fd;
	snprintf(tcp->name, sizeof(tcp->name), "TCP_%s_%d\n", buf,
		       ntohs(cli_addr->sin_port));

	for (int i = 0; i < NPORT; i++) {
		char name[512];

		snprintf (name, sizeof(name), "input_%s_%d", tcp->name, i+1);

		tcp->ports[i] = jack_port_register (jackClient, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if(tcp->ports[i]==NULL)
		{
			fprintf (stderr, "cannot register input port \"%s\"!\n", name);
			client_shutdown(tcp);
		}
		int err=jack_connect (jackClient, jack_port_name (tcp->ports[i]), port_target_names[i]);
		if(err)
		{
			fprintf (stderr, "cannot connect input port %s to %s\n", jack_port_name (tcp->ports[i]), port_target_names[i]);
		}
	}
	{
	uint8_t *buffer=calloc(CLIENT_RINGBUFFER_BYTES, 1);
	assert(buffer!=NULL);
	ringBuffer_create(&(tcp->rb), CLIENT_RINGBUFFER_BYTES, buffer);
	}
	{
	uint8_t *buffer=calloc(SERVER_RINGBUFFER_BYTES, 1);
	assert(buffer!=NULL);
	ringBuffer_create(&(tcp->audio), SERVER_RINGBUFFER_BYTES, buffer);
	}
	{
	uint8_t *buffer=calloc(SERVER_RINGBUFFER_BYTES, 1);
	assert(buffer!=NULL);
	ringBuffer_create(&(tcp->audioOriginal), SERVER_RINGBUFFER_BYTES, buffer);
	}
	pthread_mutex_lock(&tcpClients_list_lock);
	linked_list_add(&tcpClients, &(tcp->list));
	pthread_mutex_unlock(&tcpClients_list_lock);
	epoll_ctl_add(epfd, fd,
		      EPOLLIN | EPOLLET | EPOLLRDHUP |
		      EPOLLHUP, tcp);
}

/// Linux SIGNAL handler to gracefully handle ctrl-c
void intHandler(int dummy) {
	exitProgram=true;
}
/// Count all received audio bytes for debugging purpose
static uint32_t receivedAudioBytes=0;
/// find the minimum value of two helper function.
static uint32_t min_u32(uint32_t a, uint32_t b)
{
	return a<b?a:b;
}
/// Size maximum number of float samples when resampling input and output buffer.
/// The value could be anything in theory but it may have effect on performance.
/// Buffers are allocated on stack that limits the maximum value
#define RESAMPLE_BUFFER_SIZE 128
/// Process all data in audioOriginal and write the resampled data into "audio"
/// Run until source is empty or target is full
static void resample(tcpClient * client)
{
	float input_frame[RESAMPLE_BUFFER_SIZE];
	float output_frame[RESAMPLE_BUFFER_SIZE];
	while(true)
	{
		uint32_t avrb=ringBuffer_availableRead(&(client->audioOriginal));
		uint32_t avwb=ringBuffer_availableWrite(&(client->audio));
		spx_uint32_t in_len=min_u32(RESAMPLE_BUFFER_SIZE/NPORT, avrb/NPORT/sizeof(float));
		spx_uint32_t out_len=min_u32(RESAMPLE_BUFFER_SIZE/NPORT, avwb/NPORT/sizeof(float));
		// printf("resample av: %d %d %d %d\n", avrb, avwb, in_len, out_len);
		if(out_len<1||in_len<1)
		{
			// printf("resample ret\n");
			return;
		}
		/// Read data but do not advance read pointer: we don't yet know the number of samples actually processed
		ringBuffer_peek(&(client->audioOriginal), in_len*NPORT*sizeof(float), (uint8_t *)&(input_frame[0]));
		assert(client->resampler_state!=NULL);
		int err=speex_resampler_process_interleaved_float(client->resampler_state,
										 &(input_frame[0]), // const spx_int16_t *in,
										 &in_len, //spx_uint32_t *in_len,
										 &(output_frame[0]), //spx_int16_t *out,
										 &out_len //spx_uint32_t *out_len
						);
		/// Consume the processed data. The data is already read we just adjust the pointer without copying data
		ringBuffer_read(&(client->audioOriginal), in_len*NPORT*sizeof(float), NULL);
		/// Write the created samples into the output
		ringBuffer_write(&(client->audio), out_len*NPORT*sizeof(float), (uint8_t *)&(output_frame[0]));
		client->countSamples+=out_len/NPORT;

		/// Check the current buffered length of samples and update resampler to control the buffer length around the target length.
		uint32_t fill=ringBuffer_availableRead(&(client->audio));
		float seconds=((float)fill)/NPORT/SAMPLE_SIZE_BYTES/samplerate;

		if(client->countSamples>48000)
		{
			// Log length of buffer to stdout ~every second once.
			printf("Seconds buffered: %f\n", seconds);
			client->countSamples=0;
		}
		if(seconds>SERVER_BUFFER_SECONDS*1.4)
		{
			speex_resampler_set_rate(client->resampler_state, (uint32_t)(1.03*client->samplerate), samplerate);
		}else if(seconds>SERVER_BUFFER_SECONDS*1.2)
		{
			speex_resampler_set_rate(client->resampler_state, (uint32_t)(1.01*client->samplerate), samplerate);
		}
		else if(seconds<SERVER_BUFFER_SECONDS*.6)
		{
			speex_resampler_set_rate(client->resampler_state, (uint32_t)(0.97*client->samplerate), samplerate);
		}else if(seconds<SERVER_BUFFER_SECONDS*.8)
		{
			speex_resampler_set_rate(client->resampler_state, (uint32_t)(0.99*client->samplerate), samplerate);
		}else
		{
			speex_resampler_set_rate(client->resampler_state, client->samplerate, samplerate);
		}

		/// Start playback when desired buffer length was reached
		if(!client->started && seconds>=SERVER_BUFFER_SECONDS)
		{
			client->started=true;
		}

		/// Log resampler error - in case it actually happens the logging should be improved
		if(err!=0)
		{
			printf("speex_resampler_process_interleaved_float error: %d", err);
		}
	}
}
/// Read the raw tcp stream in "rb" buffer and parse messages and process them.
/// Audio data messages result in putting remote audio data (remote samplerate) to audioOriginal buffer.
/// @return true means there was an error in the stream and client was disposed
static bool process_messages(tcpClient * client)
{
	uint32_t ar=ringBuffer_availableRead(&(client->rb));
	while(ar>=sizeof(struct chunk_header))
	{
		struct chunk_header header;
		ringBuffer_peek(&(client->rb), (uint32_t)sizeof(struct chunk_header), (uint8_t *)&header );
		if(ar>=header.payload+sizeof(struct chunk_header))
		{
			// Message fully received
			// pa_log("TCP server msg received: %d %d", header.type, header.payload);
			ringBuffer_read(&(client->rb), (uint32_t)sizeof(struct chunk_header), NULL );
			switch(header.type)
			{
			case R_MSG_AUDIO_CHUNK:
			{
				int aw=ringBuffer_availableWrite(&(client->audioOriginal));
				if(aw>=header.payload)
				{
					uint32_t remaining=header.payload;
					uint8_t * data;
					while(remaining>0)
					{
						int n=ringBuffer_accessWriteBuffer(&(client->audioOriginal), &data, remaining);
						ringBuffer_read(&(client->rb), n, data );
						ringBuffer_write(&(client->audioOriginal), n, NULL);
						remaining-=n;
					}
					receivedAudioBytes+=header.payload;
					while(receivedAudioBytes>44100)
					{
//						printf("Received %d bytes audio \n", receivedAudioBytes);
						receivedAudioBytes-=44100;
					}
					resample(client);
				}else
				{
					// Overflow - just omit data
					ringBuffer_read(&(client->rb), header.payload, NULL );
				}
				break;
			}
			case R_MSG_STREAM_PARAMETERS:
			{
				struct stream_parameters params;
				ringBuffer_read(&(client->rb), (uint32_t)sizeof(struct stream_parameters)-sizeof(struct chunk_header), ((uint8_t *)&params)+sizeof(struct chunk_header) );
				client->samplerate=(uint32_t)(params.samplerate);
				int err;
				printf("Sample rate: %d %d\n", client->samplerate, samplerate);
				client->resampler_state=speex_resampler_init( NPORT, //spx_uint32_t nb_channels,
													client->samplerate, //spx_uint32_t in_rate,
			                                          samplerate, //spx_uint32_t out_rate,
			                                          10, // int quality [0,10] 10 is best,
			                                          &err// int *err
								);
				assert(client->resampler_state!=NULL);
				break;
			}
			default:
				printf("Unknown header type: %d", header.type);
				client_shutdown(client);
				return true;
				break;
			}
		}else
		{
			/// In case the message is not fully received yet then exit processing loop: the message will be processed when process_messages is next called
			break;
		}
		ar=ringBuffer_availableRead(&(client->rb));
	}
	return false;
}
/// Parse parameters, open Jack client, open TCP server and process epoll events (client connected, data on TCP streams).
int main(int argc, char *argv[])
{
	int i;
	int n;
	int nfds;
	int listen_sock;
	int conn_sock;
	int socklen;
	struct sockaddr_in srv_addr;
	struct sockaddr_in cli_addr;
	struct epoll_event events[MAX_EVENTS];
	int port=DEFAULT_PORT;
	tcpServer server;

	char *optstring = "b:h";
	struct option long_options[] = {
		{ "help", 0, 0, 'h' },
		{ "baseSourceName", 1, 0, 'b' },
		{ "port", 1, 0, 'p' },
		{ 0, 0, 0, 0 }
	};
	int longopt_index = 0;
	int show_usage = 0;
	char c;
	while ((c = getopt_long (argc, argv, optstring, long_options, &longopt_index)) != -1) {
		switch (c) {
		case 1:
			/* getopt signals end of '-' options */
			break;
		case 'h':
			show_usage++;
			break;
		case 'b':
			for(int i=0;i<NPORT;++i)
			{
				snprintf(port_target_names[i], 128, "%s%d", optarg, i);
			}
			printf("basename: %s\n", optarg);
			break;
		case 'p':
			printf("port: %s\n", optarg);
			port=atoi(optstring);
			break;
		default:
			fprintf (stderr, "error\n");
			show_usage++;
			break;
		}
	}
	for(int i=0;i<NPORT;++i)
	{
		printf("Selected port to connect to: '%s'\n", port_target_names[i]);
	}
	printf("TCP port to start server on: %d\n", port);
	if (show_usage) {
		fprintf (stderr, "usage: jack-tcp-server [ -b baseSourceName ] [-p port]\n");
		exit (1);
	}

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);

	jackClient = jack_client_open ("TCP server", JackNullOption, NULL);
	assert(jackClient != NULL);

	signal(SIGINT, intHandler);

	jack_set_process_callback (jackClient, jack_process_frames_callback, NULL);
	jack_on_shutdown (jackClient, jack_shutdown, NULL);

	assert(!jack_activate (jackClient));

	samplerate = jack_get_sample_rate(jackClient);

	set_sockaddr(&srv_addr, port);
	int err=bind(listen_sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	assert(err==0);

	setnonblocking(listen_sock);
	listen(listen_sock, 16);

	epfd = epoll_create(1);
	epoll_ctl_add(epfd, listen_sock, EPOLLIN | EPOLLOUT | EPOLLET, &server);

	socklen = sizeof(cli_addr);
	while(!exitProgram) {
		nfds = epoll_wait(epfd, events, MAX_EVENTS, 250);
		for (i = 0; i < nfds; i++) {
			if (events[i].data.ptr == &server) {
				/* handle new connection */
				conn_sock =
				    accept(listen_sock,
					   (struct sockaddr *)&cli_addr,
					   &socklen);
				setnonblocking(conn_sock);
				openClient(conn_sock, &cli_addr);
			} else if (events[i].events & EPOLLIN) {
				tcpClient * client = events[i].data.ptr;
				if(client!=NULL)
				{
					/* handle EPOLLIN event */
					for (;;) {
						uint32_t awW=ringBuffer_availableWrite(&(client->rb));
						if(awW<1)
						{
							break;
						}
						uint8_t * buffer;
			 		    int l =ringBuffer_accessWriteBuffer(&(client->rb), &buffer, awW);
						n = read(client->fd, buffer, l);
						if(n==0)
						{
							if(errno!=EAGAIN)
							{
								printf("Shutdown:\n");
								client_shutdown(client);
							}
							break;
						}else if (n < 0 /* || errno == EAGAIN */ )
						{
							if(errno!=EAGAIN)
							{
								printf("Shutdown 2 %d:\n", errno);
								client_shutdown(client);
							}
							break;
						} else {
							ringBuffer_write(&(client->rb), n, NULL);
							if(process_messages(client))
							{
								break;
							}
						}
					}
				}
			} else if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				tcpClient * client = events[i].data.ptr;
				if(client!=NULL)
				{
					client_shutdown(client);
				}
			}
		}
	}
	close(listen_sock);
	while(tcpClients!=NULL)
	{
		client_shutdown((tcpClient *)tcpClients);
	}
	printf("\nGraceful shutdown.\n");
	return 0;
}
