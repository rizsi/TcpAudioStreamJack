#include "ringBuffer.h"
#include <string.h>

void ringBuffer_create(ringBuffer_t * ringBuffer, uint32_t bufferSize, uint8_t * buffer)
{
	ringBuffer->ptrRead=0;
	ringBuffer->ptrWrite=0;
	ringBuffer->bufferSize=bufferSize;
	ringBuffer->buffer=buffer;
}

bool ringBuffer_write(ringBuffer_t * ringBuffer, uint32_t nBytes, uint8_t * data)
{
	if(ringBuffer->buffer!=NULL && ringBuffer_availableWrite(ringBuffer)>=nBytes)
	{
		uint32_t at=ringBuffer->ptrWrite;
		uint32_t newPtr=at+nBytes;
		if(newPtr>ringBuffer->bufferSize)
		{
			uint32_t firstSize=ringBuffer->bufferSize-at;
			uint32_t secondSize=nBytes-firstSize;
			if(data!=NULL)
			{
				memcpy(&(ringBuffer->buffer[at]), data, firstSize);
			}
			at=0;
			if(data!=NULL)
			{
				memcpy(&(ringBuffer->buffer[at]), &(data[firstSize]), secondSize);
			}
			at=secondSize;
			ringBuffer->ptrWrite=at;
		}else
		{
			if(data!=NULL)
			{
				memcpy(&(ringBuffer->buffer[at]), data, nBytes);
			}
			at+=nBytes;
			if(at==ringBuffer->bufferSize)
			{
				at=0;
			}
			ringBuffer->ptrWrite=at;
		}
		return true;
	}else
	{
		return false;
	}
}
bool ringBuffer_read(ringBuffer_t * ringBuffer, uint32_t nBytes, uint8_t * data)
{
	if(ringBuffer_availableRead(ringBuffer)>=nBytes)
	{
		uint32_t at=ringBuffer->ptrRead;
		uint32_t newPtr=at+nBytes;
		if(newPtr>ringBuffer->bufferSize)
		{
			uint32_t firstSize=ringBuffer->bufferSize-at;
			uint32_t secondSize=nBytes-firstSize;
			if(data!=NULL)
			{
			  memcpy(data, &(ringBuffer->buffer[at]), firstSize);
			}
			at=0;
      if(data!=NULL)
      {
        memcpy(&(data[firstSize]), &(ringBuffer->buffer[at]), secondSize);
      }
			at=secondSize;
			ringBuffer->ptrRead=at;
		}else
		{
      if(data!=NULL)
      {
        memcpy(data, &(ringBuffer->buffer[at]), nBytes);
      }
			at+=nBytes;
			if(at==ringBuffer->bufferSize)
			{
				at=0;
			}
			ringBuffer->ptrRead=at;
		}
		return true;
	}else
	{
		return false;
	}
}
bool ringBuffer_peek(ringBuffer_t * ringBuffer, uint32_t nBytes, uint8_t * data)
{
	if(ringBuffer_availableRead(ringBuffer)>=nBytes)
	{
		uint32_t at=ringBuffer->ptrRead;
		uint32_t newPtr=at+nBytes;
		if(newPtr>ringBuffer->bufferSize)
		{
			uint32_t firstSize=ringBuffer->bufferSize-at;
			uint32_t secondSize=nBytes-firstSize;
			memcpy(data, &(ringBuffer->buffer[at]), firstSize);
			at=0;
			memcpy(&(data[firstSize]), &(ringBuffer->buffer[at]), secondSize);
			at=secondSize;
		}else
		{
			memcpy(data, &(ringBuffer->buffer[at]), nBytes);
			at+=nBytes;
			if(at==ringBuffer->ptrWrite)
			{
				at=0;
			}
		}
		return true;
	}else
	{
		return false;
	}
}
bool ringBuffer_peekOffset(ringBuffer_t * ringBuffer, uint32_t offset, uint32_t nBytes, uint8_t * data)
{
  if(ringBuffer_availableRead(ringBuffer)>=nBytes+offset)
  {
    uint32_t at=ringBuffer->ptrRead+offset;
    if(at>=ringBuffer->bufferSize)
    {
      at-=ringBuffer->bufferSize;
    }
    uint32_t newPtr=at+nBytes;
    if(newPtr>ringBuffer->bufferSize)
    {
      uint32_t firstSize=ringBuffer->bufferSize-at;
      uint32_t secondSize=nBytes-firstSize;
      memcpy(data, &(ringBuffer->buffer[at]), firstSize);
      at=0;
      memcpy(&(data[firstSize]), &(ringBuffer->buffer[at]), secondSize);
      at=secondSize;
    }else
    {
      memcpy(data, &(ringBuffer->buffer[at]), nBytes);
      at+=nBytes;
      if(at==ringBuffer->ptrWrite)
      {
        at=0;
      }
    }
    return true;
  }else
  {
    return false;
  }
}
uint32_t ringBuffer_accessReadBuffer(ringBuffer_t * ringBuffer, uint8_t ** ptrBuffer, uint32_t maxBytes)
{
  uint32_t nBytes=ringBuffer_availableRead(ringBuffer);
  if(nBytes>maxBytes)
  {
    nBytes=maxBytes;
  }
  if(nBytes>0)
  {
    uint32_t at=ringBuffer->ptrRead;
    *ptrBuffer=&(ringBuffer->buffer[at]);
    uint32_t newPtr=at+nBytes;
    if(newPtr>ringBuffer->bufferSize)
    {
      uint32_t firstSize=ringBuffer->bufferSize-at;
      return firstSize;
    }else
    {
      return nBytes;
    }
  }else
  {
    return 0u;
  }
}
uint32_t ringBuffer_accessWriteBuffer(ringBuffer_t * ringBuffer, uint8_t ** ptrBuffer, uint32_t maxBytes)
{
  uint32_t nBytes=ringBuffer_availableWrite(ringBuffer);
  if(nBytes>maxBytes)
  {
    nBytes=maxBytes;
  }
  if(nBytes>0)
  {
    uint32_t at=ringBuffer->ptrWrite;
    *ptrBuffer=&(ringBuffer->buffer[at]);
    uint32_t newPtr=at+nBytes;
    if(newPtr>ringBuffer->bufferSize)
    {
      uint32_t firstSize=ringBuffer->bufferSize-at;
      return firstSize;
    }else
    {
      return nBytes;
    }
  }else
  {
    return 0u;
  }
}


uint32_t ringBuffer_availableWrite(ringBuffer_t * ringBuffer)
{
	uint32_t fill=ringBuffer_availableRead(ringBuffer);
	return ringBuffer->bufferSize-1-fill;
}

uint32_t ringBuffer_availableRead(ringBuffer_t * ringBuffer)
{
	uint32_t ret=ringBuffer->ptrWrite+ringBuffer->bufferSize-ringBuffer->ptrRead;
	if(ret>=ringBuffer->bufferSize)
	{
		ret-=ringBuffer->bufferSize;
	}
	return ret;
}

void ringBuffer_clear(ringBuffer_t * ringBuffer)
{
  ringBuffer->buffer=NULL;
  ringBuffer->bufferSize=0;
  ringBuffer->ptrRead=0;
  ringBuffer->ptrWrite=0;
}
bool ringBuffer_isCreated(ringBuffer_t * ringBuffer)
{
  return ringBuffer->buffer!=NULL;
}

