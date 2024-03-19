#ifndef TCP_PROTOCOL_H_
#define TCP_PROTOCOL_H_
/// TCP audio stream types and constants shared between jack-tcp-client.c and jack-tcp-server.c

#define DEFAULT_PORT    8080

/// The target length of buffered data on the server (which plays the sound on real hardware)
/// Playback speed is controlled so that this length is maintained on the long run
#define SERVER_BUFFER_SECONDS (1.0f)

/// Number of ports (stereo) to connect
#define NPORT 2
/// Size of a single sample in bytes. Jack default is 32 bit float
#define SAMPLE_SIZE_BYTES sizeof(jack_default_audio_sample_t)

/// Estimated sample rate. Used to allocate buffer sizes. Can be different than real sample rate but should not be significantly less
/// Because then the buffers will be too small
#define SAMPLERATE 48000

/// On the client use this buffer size in bytes.
/// Must be significantly more than a single Jack chunk so that Jack process callback can always write data without blocking.
/// Must be significantly more than the samples in a single CLIENT_PERIOD_TIME_US loop
#define CLIENT_RINGBUFFER_BYTES (65536)

/// Client main loop timing is driven by a sleep. This is the timeout of this sleep.
#define CLIENT_PERIOD_TIME_US (10l*1000l)

/// Number of bytes size of the server ringbuffer. The server aims to buffer SERVER_BUFFER_SECONDS of audio data.
/// Size is twice the aimed buffer length.
#define SERVER_RINGBUFFER_BYTES ((int)(SAMPLERATE*NPORT*SAMPLE_SIZE_BYTES*SERVER_BUFFER_SECONDS*2))

/// Message type Audio samples. Format is struct chunk_header + jack_default_audio_sample_t samples. Samples from channels are interleaved.
#define R_MSG_AUDIO_CHUNK 1
/// Message type set stream parameters. Must be the first message to send. Format is: struct stream_parameters
#define R_MSG_STREAM_PARAMETERS 2

/// On the TCP stream all messages are prefixed with this.
struct chunk_header {
	/// Type of this message. See R_MSG_... constants
	uint32_t type;
	/// Size of the payload of this message
	uint32_t payload;
} __attribute__((packed));

/// The R_MSG_STREAM_PARAMETERS message structure
struct stream_parameters {
	struct chunk_header head;
	uint32_t samplerate;
	uint32_t nchannel;
	uint32_t sampletype;
} __attribute__((packed));


#endif /* TCP_PROTOCOL_H_ */
