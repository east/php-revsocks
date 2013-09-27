#ifndef SOCKS_SOCKS_SERVER_H
#define SOCKS_SOCKS_SERVER_H

#include <arpa/inet.h> /* struct sockaddr_in */

enum
{
	S5SRV_MAX_CLIENTS=256,

	/* errors */
	S5SRV_ERROR_UNKNOWN=1,
	S5SRV_ERROR_CREATING_SOCKET_FAILED,
	S5SRV_ERROR_BINDING_SOCKET,
	S5SRV_ERROR_LISTENING_SOCKET,
	S5SRV_ERROR_NON_BLOCK,

	/* S5SRV_CL states */
	S5SRV_CL_OFFLINE=0,
	S5SRV_CL_ACCEPTED,
	S5SRV_CL_RECV_REQUEST,
	S5SRV_CL_CONN_PENDING,
	S5SRV_CL_ONLINE,
	S5SRV_CL_SHUTDOWN,

	/* cb_conn_state returns */
	S5SRV_CB_CONN_PENDING=0,
	S5SRV_CB_CONN_FAILED,
	S5SRV_CB_CONN_ONLINE,
};

/* socks5 connect request */
typedef struct
{
	int addr_type;
	char data[128]; /* domain name / ip */
	int port;
} S5SRV_CONN_REQ;

typedef struct
{
	int state;
	int socket;
	struct sockaddr_in addr;
	S5SRV_CONN_REQ req;
	void *conn_cb_user;
} S5SRV_CL;

/* callbacks */
/* the socks server calls cb_connect when a client wants to establish a connection */
/* return: zero = accepted, nonzero = decline  */
typedef int (*cb_new_connect)(S5SRV_CL *cl, void **userdata);
/* this callback returns the state of the connection */
/* return: enums */
typedef int (*cb_conn_state)(void *userdata);
/* data receive callback */
typedef void (*cb_on_data)(void *userdata, char *data, int size);
/* disconnected */
typedef void (*cb_on_disc)(void *userdata);
/* receive data from server. the size of data is stored int *size. */
/* the size of received data need to be stored in *size. */
/* *size = -1 indicates a server disconnect */
typedef void (*cb_get_data)(void *userdata, char *data, int *size);

typedef struct
{
	int error;
	int socket;
	int num_clients;
	S5SRV_CL clients[S5SRV_MAX_CLIENTS];

	/* callbacks */
	cb_new_connect new_connect;
	cb_conn_state conn_state;
	cb_on_data on_data;
	cb_on_disc on_disc;
	cb_get_data get_data;
} S5SRV;

int s5srv_init(S5SRV *srv, const char *bind_host, int listening_port, cb_new_connect new_connect, cb_conn_state conn_state, cb_on_data on_data, cb_on_disc on_disc, cb_get_data get_data);
void s5srv_close(S5SRV *srv);
int s5srv_tick(S5SRV *srv, int block);
int s5srv_get_fds(S5SRV *srv, fd_set *rfds, fd_set *wfds);
int s5srv_tick_ex(S5SRV *srv, fd_set *rfds, fd_set *wfds);
/* size = 0 means connection close */
void s5srv_send(S5SRV_CL *cl, char *data, size_t size);

#endif
