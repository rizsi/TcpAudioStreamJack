/* 
 * Open a TCP client and send recorded sound to the TCP server
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include "tcp-protocol.h"
#include "ringBuffer.h"

/// The Jack audio client instance.
static jack_client_t *jackClient;
/// Jack port identifiers used to capture audio data
jack_port_t * ports[NPORT];

/// Graceful exit requested by the user - not implemented because just killing the process is good enough.
static volatile bool exitProgram=false;
/// Signal that the client is connected to the server and audio frame data has to be put into the tcpStream ringbuffer
/// It is false while retrying connection and the main thread can safely flush the buffer and put the initializing message before enabling
/// enqueuing audio data
static volatile bool running=false;

/// Port names that are connected as source to the recording ports created by this process
static char source_port_names[][128]={"null-sink Audio/Sink sink:monitor_0", "null-sink Audio/Sink sink:monitor_1"};

/// Data to be sent through the TCP stream. messages are prefixed with struct chunk_header
/// Written by the jack thread and read on the main thread to copy data into the TCP client stream
/// In the beginning of the stream the main thread also writes a message before activating the jack input
ringBuffer_t tcpStream;

/// Fill struct sockaddr_in type INET address object from name of server and port number. (Includes blocking name resolution using gethostbyname)
bool mksin (struct sockaddr_in *sinp, const char *host, int port)
{
	struct hostent *hp;
	bzero (sinp, sizeof (*sinp));
	sinp->sin_family = AF_INET;
	sinp->sin_port = htons (port);
	if (!(hp = gethostbyname (host))) {
		fprintf (stderr, "%s: bad host name\n", host);
		return true;
	}
	memcpy (&sinp->sin_addr, hp->h_addr, sizeof (sinp->sin_addr));
	return false;
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
	uint32_t payload=nframes * SAMPLE_SIZE_BYTES * NPORT;
	int req=sizeof(struct chunk_header) + payload;
	if(ringBuffer_availableWrite(&tcpStream) >=req && running)
	{
		struct chunk_header header;
		header.type=R_MSG_AUDIO_CHUNK;
		header.payload=payload;
		ringBuffer_write(&tcpStream, (uint32_t)sizeof(struct chunk_header), (uint8_t *)&header);
		jack_default_audio_sample_t * buff[NPORT];
		for(int i=0;i<NPORT;++i)
		{
			buff[i]=(jack_default_audio_sample_t *)jack_port_get_buffer(ports[i], nframes);
		}
		for(int j=0;j<nframes;++j)
		{
			for(int i=0;i<NPORT;++i)
			{
				ringBuffer_write(&tcpStream, SAMPLE_SIZE_BYTES, (uint8_t *)(buff[i]+j));
			}
		}
	}
	return 0;
}

/// Jack shutdown callback - with pipewire it is never called in my experience
/// When Jack shutdown happens there is nothing to do but exit the program.
static void jack_shutdown (void * arg)
{
	printf("jack_shutdown\n");
	exit(0);
}

/// Process arguments, open Jack connection, create Jack input ports and connect them to required source ports.
/// Then in an endless loop open client TCP socket and process connection. Connection is auto-retried after timeout after previous connection failed.
int main(int argc, char *argv[])
{
	int n;
	int c;
	int sockfd;
	struct sockaddr_in srv_addr;
	char hostname[128]="localhost";
	int port=DEFAULT_PORT;

	char *optstring = "u:b:h";
	struct option long_options[] = {
		{ "help", 0, 0, 'h' },
		{ "URL", 1, 0, 'u' },
		{ "baseSourceName", 1, 0, 'b' },
		{ 0, 0, 0, 0 }
	};
	int longopt_index = 0;
	int show_usage = 0;
	while ((c = getopt_long (argc, argv, optstring, long_options, &longopt_index)) != -1) {
		switch (c) {
		case 1:
			/* getopt signals end of '-' options */
			break;

		case 'h':
			show_usage++;
			break;
		case 'u':
		{
			printf("url: %s\n", optarg);
			char * colon =strchr(optarg, ':');
			if(colon==NULL)
			{
				strncpy(hostname, optarg, sizeof(hostname));
			}else
			{
				*colon='\0';
				strncpy(hostname, optarg, sizeof(hostname));
				port=atoi(colon+1);
			}
			break;
		}
		case 'b':
			for(int i=0;i<NPORT;++i)
			{
				snprintf(source_port_names[i], 128, "%s%d", optarg, i);
			}
			printf("basename: %s\n", optarg);
			break;
		default:
			fprintf (stderr, "error\n");
			show_usage++;
			break;
		}
	}
	if (show_usage) {
		fprintf (stderr, "usage: jack-tcp-client -u serverHost:port [ -b baseSourceName ]\n");
		exit (1);
	}

	printf("TCP client host: %s port: %d\n", hostname, port);
	for(int i=0;i<NPORT;++i)
	{
		printf("Source name: '%s'\n", source_port_names[i]);
	}

	jackClient = jack_client_open ("TCP client", JackNullOption, NULL);
	assert(jackClient != NULL);

	jack_set_process_callback (jackClient, jack_process_frames_callback, NULL);
	jack_on_shutdown (jackClient, jack_shutdown, NULL);

	assert(!jack_activate (jackClient));

	jack_nframes_t samplerate = jack_get_sample_rate (jackClient);
	uint8_t * buffer=calloc(CLIENT_RINGBUFFER_BYTES, 1);
	assert(buffer!=NULL);
	ringBuffer_create(&tcpStream, CLIENT_RINGBUFFER_BYTES, buffer);

	for (int i = 0; i < NPORT; i++) {
		char name[512];

		snprintf (name, sizeof(name), "output_TCP_%d", i+1);

		ports[i] = jack_port_register (jackClient, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		if(ports[i]==NULL)
		{
			fprintf (stderr, "cannot register input port \"%s\"!\n", name);
			exit(1);
		}
		int err=jack_connect (jackClient, source_port_names[i], jack_port_name (ports[i]));
		if(err)
		{
			fprintf (stderr, "cannot connect input port %s to %s\n", jack_port_name (ports[i]), source_port_names[i]);
		}
	}
	bool first=true;
	while(!exitProgram)
	{
		if(!first)
		{
			usleep(1000*1000);
		}
		first=false;
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		mksin(&srv_addr, hostname, port);
		int err=connect(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
		if(err!=0)
		{
			perror("connect()");
			usleep(1000*1000);
		}else
		{
			// Empty ringbuffer. Because running == false it is safe to just read all data the next data will start at the beginning of a frame
			ringBuffer_read(&tcpStream, ringBuffer_availableRead(&tcpStream), NULL);
			struct stream_parameters params;
			params.head.type=R_MSG_STREAM_PARAMETERS;
			params.head.payload=sizeof(struct stream_parameters) - sizeof(struct chunk_header);
			params.samplerate=samplerate;
			params.nchannel=NPORT;
			params.sampletype=0;
			ringBuffer_write(&tcpStream, sizeof(struct stream_parameters), (uint8_t *)&params);
			bool tcpBroken=false;
			setnonblocking(sockfd);
			running=true;
			printf("Connected to server\n");
			while(!exitProgram && !tcpBroken) {
				int awr=ringBuffer_availableRead(&tcpStream);
				while(awr>0)
				{
					uint8_t * buf;
					uint32_t len=ringBuffer_accessReadBuffer(&tcpStream, &buf, awr);
					ssize_t written = write(sockfd, buf, len);
					if(written==0)
					{
						tcpBroken=true;
						break;
					}else if(written<0)
					{
						if(errno==EAGAIN)
						{
							break;
						}else
						{
							perror("TCP write");
							tcpBroken=true;
							break;
						}
					}else
					{
						ringBuffer_read(&tcpStream, written, NULL);
					}
					awr=ringBuffer_availableRead(&tcpStream);
				}
				while(true)
				{
					uint8_t buf[32];
					ssize_t nread=read(sockfd, buf, sizeof(buf));
					if(nread==0)
					{
						tcpBroken=true;
						break;
					}
					if(nread<0)
					{
						if(errno == EAGAIN)
						{
							break;
						}else
						{
							perror("TCP read");
							tcpBroken=true;
							break;
						}
					}else
					{
						// Good - ignore data for now
					}
				}
				usleep(CLIENT_PERIOD_TIME_US);
			}
		}
		close(sockfd);
		running=false;
	}
	printf("Normal exit\n");
	return 0;
}
