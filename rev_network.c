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

static int handle_buffer(struct rev_server *revsrv, struct rev_client *cl);
static int handle_msg(struct rev_server *revsrv, struct rev_client *cl, struct netmsg *msg);
static struct netmsg *empty_msg(int id);

static struct netmsg empty_msg_obj;

int
rev_pump_network(struct rev_server *revsrv, struct rev_client *cl)
{
	/* handle/parse avaiable messages in buffer */
	while (handle_buffer(revsrv, cl));

	return 0;
}

static int
handle_buffer(struct rev_server *revsrv, struct rev_client *cl)
{
	struct FIFOBUF *inbuf = INBUF(cl);

	if (fifo_len(inbuf) < 3)
		return 0; /* not enough data */

	int msg_size = *((uint16_t*)inbuf->data);
	int msg_id = *((uint8_t*)(inbuf->data+2));

	ASSERT(msg_size <= MAX_MSG_SIZE, "invalid msg size")

	if (fifo_len(inbuf) < 3 + msg_size)
		return 0; /* not enough data */

	/* skip size & id */
	fifo_read(inbuf, NULL, 3);

	char buf[MAX_MSG_SIZE];
	struct netmsg msg;

	msg.id = msg_id;
	msg.size = msg_size;
	msg.data = buf;

	//printf("got msg id %d size %d\n", msg_id, msg_size);
	/* read and handle message */
	fifo_read(inbuf, buf, msg_size);
	ASSERT(handle_msg(revsrv, cl, &msg) == 0, "invalid msg");

	if (fifo_len(inbuf) > 0)
		return 1; /* there is data left */
	return 0;
}

static int
handle_msg(struct rev_server *revsrv, struct rev_client *cl, struct netmsg *msg)
{
	if (msg->id == MSG_DBGMSG)
		printf("dbg msg '%s'\n", msg->data);
	else if(msg->id == MSG_PING)
	{
		/* answer ping */
		printf("got ping, send pong\n");
		rev_send_msg(revsrv, cl, empty_msg(MSG_PONG));
	}
	//else if(msg->id == MSG_PONG)
	//	printf("got pong\n");
	else if(msg->id == MSG_CONN_STATE)
	{
		const char *p = msg->data;
		int id = *((uint16_t*)p);
		int state = *(p+2);
		const char *err_str = p+3;

		struct network_handle *hndl = revsrv_netw_hndl(revsrv, id);

		if (hndl->state == NETW_HNDL_TCP_CONNECT)
		{
			if (state == CONN_STATE_ERROR)
			{
				/* failed to connect */
				printf("netw handle %d failed to connect '%s'\n", id, err_str);
				hndl->state = NETW_HNDL_TCP_FAIL;
			}
			else if(state == CONN_STATE_ONLINE)
			{
				/* connection established */
				printf("netw handle %d established connection\n", id);
				hndl->state = NETW_HNDL_TCP_ONLINE;
			}
			else { ASSERT(0, "invalid state") }
		}
		else if(hndl->state == NETW_HNDL_TCP_ONLINE &&
					state == CONN_STATE_ERROR)
		{
			hndl->state = NETW_HNDL_TCP_DISC;
		}
		else if(hndl->state == NETW_HNDL_OFFLINE)
			printf("WARNING: conn state on offline handle\n");
		else { ASSERT(0, "invalid state") }
	}
	else if (msg->id == MSG_SEND)
	{
		const char *p = msg->data;
		int session_id = *((uint16_t*)p);
		int size = *((uint16_t*)(p+2));
		const char *data = p+4;

		ASSERT(msg->size == size+4, "invalid size")

		//printf("data from session %d size %d\n", session_id, size);


		struct network_handle *hndl =
				 revsrv_netw_hndl(revsrv, session_id);

		if (hndl)
		{
			int res = fifo_write(&hndl->tcp_in_buf, data, size);
			ASSERT(res == 0, "session in buf overflow")
		}
	}
	else
		return -1;
	
	return 0;
}

int
rev_send_msg(struct rev_server *revsrv, struct rev_client *cl, struct netmsg *msg)
{
	struct FIFOBUF *outbuf = OUTBUF(cl); 
	uint16_t size_16 = msg->size;
	uint8_t msg_id_8 = msg->id;

	if (fifo_space_left(outbuf) < 3 + msg->size)
		return -1; /* unable to send message (full buffer) */
	
	/* put size */
	fifo_write(outbuf, (const char*)&size_16, 2);	
	/* msg id */
	fifo_write(outbuf, (const char*)&msg_id_8, 1);
	/* data */
	if (msg->size > 0)
		fifo_write(outbuf, msg->data, msg->size);

	return 0; /* done */
}

struct netmsg*
empty_msg(int id)
{
	empty_msg_obj.id = id;
	empty_msg_obj.size = 0;
	empty_msg_obj.data = NULL;
	return &empty_msg_obj;
}

void rev_netmsg_send(struct rev_server *revsrv, struct rev_client *cl,
					int netw_hndl, const char *data, int size)
{
	struct netmsg msg;
	char buf[MAX_MSG_SIZE];

	msg.id = MSG_SEND;
	msg.size = 4 + size;
	msg.data = buf;

	*((uint16_t*)buf) = netw_hndl;
	*((uint16_t*)(buf+2)) = size;
	memcpy(buf+4, data, size);

	rev_send_msg(revsrv, cl, &msg);
}
