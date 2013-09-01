#ifndef REV_SERVER_H
#define REV_SERVER_H

#include "fifobuf.h"

enum
{
	TCP_BUF_SIZE=4096,
	MAX_HTTP_IDLERS=4,
};

struct rev_client
{
	int sock;

	/* tcp buffers */
	struct FIFOBUF rev_in_buf;
	struct FIFOBUF rev_out_buf;
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

	/* url to php script */
	char http_url[512];

	/* php sockssrv will use these to establish a connection to us */
	char bind_ip[128];
	int bind_port;

	int high_desc;
	fd_set read_fds;
	fd_set write_fds;
};

void revsrv_init(struct rev_server *revsrv, const char *bind_ip,
					int bind_port, const char *http_url);
void revsrv_run(struct rev_server *revsrv);

#endif
