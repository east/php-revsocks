#ifndef REV_SERVER_H
#define REV_SERVER_H

#include "protocol.h"
#include "fifobuf.h"

#include <netinet/in.h>

enum
{
	MAX_HTTP_IDLERS=4,
	MAX_NETWORK_HANDLES=64,
};

struct rev_client
{
	int sock;

	/* tcp buffers */
	struct FIFOBUF rev_in_buf;
	struct FIFOBUF rev_out_buf;
	char in_buf[NETWORK_BUF_SIZE];
	char out_buf[NETWORK_BUF_SIZE];
};

enum
{
	/* http idler states */
	HTTP_IDLER_OFFLINE=0,
	HTTP_IDLER_CONNECTING,
	HTTP_IDLER_ONLINE,
};

struct http_idler
{
	int state;
	int sock;
	time_t date; /* timestamp since online */
	char http_host[128];
	char http_uri[256];
	struct sockaddr_in addr;
};

enum
{
	/* network handle states */
	NETW_HNDL_OFFLINE=0,
	NETW_HNDL_TCP_INIT_CONNECT,
	NETW_HNDL_TCP_CONNECT,
	NETW_HNDL_TCP_ONLINE,
	NETW_HNDL_TCP_DISC,
	NETW_HNDL_TCP_FAIL,
};

struct network_handle
{
	int state;
	int id;
	struct rev_client *cl;
	void *user_ptr;

	struct netaddr dst_addr;

	/* tcp buffers */
	struct FIFOBUF tcp_in_buf;
	struct FIFOBUF tcp_out_buf;
	char in_buf[NETWORK_BUF_SIZE];
	char out_buf[NETWORK_BUF_SIZE];

	int terminate;
};

struct rev_server
{
	/*
	   this socket is used to send a http request
	   which initiates the php script
	*/
	struct http_idler http_idlers[MAX_HTTP_IDLERS];
	/*
	   this socket is used to accept the connection
	   requested by the php script
	*/
	int rev_listen_sock;

	/* we need two connections to rev sockssrv */
	struct rev_client clients[2];

	/* client that should be used */
	struct rev_client *usable_cl;

	/* url to php script */
	char http_url[512];

	/* php sockssrv will use these to establish a connection to us */
	char bind_ip[128];
	int bind_port;

	/*
		we expect the websrv to close the
		connection after about 60 seconds.
		the http idlers should close the connection
		before this happens
	*/
	int http_timeout;

	fd_set *read_fds;
	fd_set *write_fds;

	/* used to create http idlers periodically */
	time_t new_idler_date;
	int last_idler_lifetime;

	struct network_handle netw_hndls[MAX_NETWORK_HANDLES];
};

int revsrv_init(struct rev_server *revsrv, const char *bind_ip,
					int bind_port, const char *http_url, int http_timeout);
int revsrv_get_fds(struct rev_server *revsrv, fd_set *rfds, fd_set *wfds);
int revsrv_max_block_time(struct rev_server *revsrv);
void revsrv_tick(struct rev_server *revsrv, fd_set *rfds, fd_set *wfds);
struct rev_client *revsrv_usable_cl(struct rev_server *revsrv);
struct network_handle *revsrv_netw_hndl(struct rev_server *revsrv, int id);

/* network handle pool */
int revsrv_new_netw_hndl(struct rev_server *revsrv);
void revsrv_free_netw_hndl(struct rev_server *revsrv, int id);

/* initiate a tcp connection (returns: connection handle id) */
int revsrv_init_conn(struct rev_server *revsrv, struct netaddr *addr, void *user_ptr);

int revsrv_cl_send(struct rev_server *revsrv, int netw_handle, const char *data, int size);
void revsrv_cl_close(struct rev_server *revsrv, int netw_hndl);
int revsrv_cl_recv(struct rev_server *revsrv, int netw_hndl, char *dst, int size);

enum
{
	REV_CONN_PENDING=0,
	REV_CONN_ONLINE,
	REV_CONN_FAILED,
};

int revsrv_conn_state(struct rev_server *revsrv, int netw_hndl);

#endif
