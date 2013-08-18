//! @file

#include <string.h>

#include "fifobuf.h"

void fifo_init(struct FIFOBUF *buf, char *data, uint64_t size)
{
	buf->data = data;
	buf->size = size;
	buf->offset = 0;
}

uint64_t fifo_len(struct FIFOBUF *buf)
{
	return buf->offset;
}

uint64_t fifo_space_left(struct FIFOBUF *buf)
{
	return buf->size - buf->offset;
}

int fifo_write(struct FIFOBUF *buf, const char *data, uint64_t size)
{
	if (fifo_space_left(buf) < size)
		return -1;
	
	memcpy(buf->data+buf->offset, 
			data,
			size);
	
	buf->offset += size;
	return 0;
}

int fifo_read(struct FIFOBUF *buf, char *out, uint64_t size)
{
	if (size > fifo_len(buf))
		size = fifo_len(buf);
	
	if (size == 0)
		return 0;
	
	if (out)
		memcpy(out, buf->data, size);
	
	memmove(buf->data + size,
				buf->data,
				fifo_len(buf));
	buf->offset -= size;

	return size;
}

void fifo_clean(struct FIFOBUF *buf)
{
	buf->offset = 0;
}
