#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "system.h"
#include "rev_server.h"
#include "rev_network.h"

#define OUTBUF(x) (&x->rev_out_buf)
#define INBUF(x) (&x->rev_in_buf)

int
pump_network(struct rev_server *revsrv, struct rev_client *cl)
{
	struct FIFOBUF *inbuf = INBUF(cl);

	if (fifo_len(inbuf) < 3)
		return 0; /* not enough data */

	int msg_size = *((uint16_t*)inbuf->data);
	int msg_id = *((uint8_t*)(inbuf->data+2));

	if (fifo_len(inbuf) < 3 + msg_size)
		return 0; /* not enough data */

	/* skip size & id */
	fifo_read(inbuf, NULL, 3);

	printf("got msg id %d size %d\n", msg_id, msg_size);
	/* skip message */
	fifo_read(inbuf, NULL, msg_size);

	return 0;
}

