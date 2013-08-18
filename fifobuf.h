//! @file

#ifndef FIFO_H
#define FIFO_H

/*!
	A small first in first out buffer
*/

#include <stdint.h>

struct FIFOBUF
{
	char *data; //!< Allocated buffer*
	uint64_t size; //!< Size of allocated buffer
	uint64_t offset; //!< Current write offset
};

/*!
	Initiates a fifo buffer

	\sa fifo_init()
	\param buf Pointer to fifo buffer
	\param data Pointer to allocated buffer
	\param size Size of allocated buffer
*/
void fifo_init(struct FIFOBUF *buf, char *data, uint64_t size);

/*!
	Get the number of used bytes in the buffer
	
	\sa fifo_len()
	\param buf Pointer to fifo buffer
	\return Length of buffer
*/
uint64_t fifo_len(struct FIFOBUF *buf);

/*!
	Get the number of unused bytes in the buffer
	
	\sa fifo_space_left()
	\param buf Pointer to fifo buffer
	\return Size of free space in buffer
*/
uint64_t fifo_space_left(struct FIFOBUF *buf);

/*!
	Writes data into the buffer

	\sa fifo_write()
	\param buf Pointer to fifo buffer
	\param data Data to write
	\param size Size of data to write
	\return nonzero on error 
*/
int fifo_write(struct FIFOBUF *buf, const char *data, uint64_t size);


/*!
	Reads data from the buffer

	\sa fifo_read()
	\param buf Pointer to fifo buffer
	\param out Destination buffer
	\param size Size of data to read
	\return Number of read bytes
*/
int fifo_read(struct FIFOBUF *buf, char *out, uint64_t size);

/*!
	Sets the current offset to zero
	\sa fifo_clean()
	\param buf Pointer to fifo buffer
*/
void fifo_clean(struct FIFOBUF *buf);

#endif /*FIFO_H*/
