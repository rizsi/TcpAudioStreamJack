#ifndef SIMULATOR_RINGBUFFER_H
#define SIMULATOR_RINGBUFFER_ H

/// ringbuffer implementation used by the simulator to store channel events

#include "simulator_types.h"

/// Structure to store fields of a ringbuffer object.
typedef struct
{
	//! ptrRead==ptrWrite means empty
  volatile uint32_t ptrRead;
	volatile uint32_t ptrWrite;
	uint32_t bufferSize;
	uint8_t * buffer;
} ringBuffer_t;

/// Initialize the given structure with initial values: empty ringbuffer
/// @param ringBuffer user provided static storage of the ringbuffer object
/// @param bufferSize size of the buffer used to store data (bufferSize-1 bytes can be used due to implementation details)
/// @param buffer static allocated buffer to store actual data
void ringBuffer_create(ringBuffer_t * ringBuffer, uint32_t bufferSize, uint8_t * buffer);

/// Set buffer pointer to NULL - ringbuffer is in not usabe state - this is not standard feature of ringbuffer implementations
void ringBuffer_clear(ringBuffer_t * ringBuffer);
/// Is this ringbuffer in created state? - this is not standard of ringbuffer implementations
bool ringBuffer_isCreated(ringBuffer_t * ringBuffer);

/// Write data into the ringbuffer
/// @param data may be NULL. In this case the write pointer is advaced without copying data (useful together with accessWriteBuffer)
/// @return false means there was not enough space in the ringbuffer and nothing was written. true means the data was fully written into the
/// buffer
bool ringBuffer_write(ringBuffer_t * ringBuffer, uint32_t nBytes, uint8_t * data);
/// Read data from ringbuffer and update the read pointer.
/// @param data pointer of target buffer to write data to. NULL is allowed and means data is not read just skipped but read pointer is updated
/// @return true means there was enough data in the buffer and read was done and read pointer was increased. false means there was not enough data and the state of the buffer was not changed.
bool ringBuffer_read(ringBuffer_t * ringBuffer, uint32_t nBytes, uint8_t * data);
/// Read data from ringbuffer and leave the read pointer unchanged.
/// @return true means there was enough data in the buffer. false means there was not enough data and the data buffer is unchanged.
bool ringBuffer_peek(ringBuffer_t * ringBuffer, uint32_t nBytes, uint8_t * data);
/// Read data from ringbuffer from a given offset and leave the read pointer unchanged.
/// @return true means there was enough data in the buffer. false means there was not enough data and the data buffer is unchanged.
bool ringBuffer_peekOffset(ringBuffer_t * ringBuffer, uint32_t offset, uint32_t nBytes, uint8_t * data);
/// Access data in the read part of the ringbuffer. Useful to implement no copy read from the ringbuffer.
/// @param[out] ptrBuffer Will be set to the current buffer position
/// @param maxBytes the maximum number of bytes to be processed in a single transaction
/// @return number of bytes accessible by the pointer. Can be less than all bytes available because when read pointer is reset to 0 then the data is only accessible in two continuous parts
uint32_t ringBuffer_accessReadBuffer(ringBuffer_t * ringBuffer, uint8_t ** ptrBuffer, uint32_t maxBytes);
/// Access data in the write part of the ringbuffer. Useful to implement no copy write to the ringbuffer.
/// @param[out] ptrBuffer Will be set to the current buffer position
/// @param maxBytes the maximum number of bytes to be processed in a single transaction
/// @return number of bytes accessible by the pointer. Can be less than all bytes available because when write pointer is reset to 0 then the data is only accessible in two continuous parts
uint32_t ringBuffer_accessWriteBuffer(ringBuffer_t * ringBuffer, uint8_t ** ptrBuffer, uint32_t maxBytes);
/// Get the number of available bytes to write
uint32_t ringBuffer_availableWrite(ringBuffer_t * ringBuffer);
/// Get the number of available bytes to read
uint32_t ringBuffer_availableRead(ringBuffer_t * ringBuffer);

#endif

