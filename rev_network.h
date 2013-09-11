#ifndef REV_NETWORK_H
#define REV_NETWORK_H

/* network handling */

#include "rev_server.h" /* struct rev_client */

int pump_network(struct rev_server *revsrv, struct rev_client *cl);

#endif
