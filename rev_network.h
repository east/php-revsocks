#ifndef REV_NETWORK_H
#define REV_NETWORK_H

/* network handling */

#include "protocol.h"
#include "rev_server.h" /* struct rev_client */

enum
{
	MAX_MSG_SIZE=BUF_SIZE,
};

struct netmsg
{
	int id;
	int size;
	const char *data;
};

int rev_pump_network(struct rev_server *revsrv, struct rev_client *cl);
int rev_send_msg(struct rev_server *revsrv, struct rev_client *cl, struct netmsg *msg);

#endif
