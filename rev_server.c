#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netdb.h>

#include "system.h"
#include "regex_url.h"
#include "rev_server.h"

static void
init_client(struct rev_client *cl)
{
	cl->sock = -1;

	fifo_init(&cl->rev_out_buf, malloc(TCP_BUF_SIZE), TCP_BUF_SIZE);
	fifo_init(&cl->rev_out_buf, malloc(TCP_BUF_SIZE), TCP_BUF_SIZE);
}

static void
reset_http_idlers(struct rev_server *revsrv)
{
	int i;
	for (i = 0; i < MAX_HTTP_IDLERS; i++)
	{
		revsrv->http_idlers[i].state = HTTP_IDLER_OFFLINE;
		revsrv->http_idlers[i].sock = -1;
		revsrv->http_idlers[i].date = 0;
	}
}

void revsrv_init(struct rev_server *revsrv, const char *bind_ip,
					int bind_port, const char *http_url)
{
	reset_http_idlers(revsrv);
	revsrv->rev_listen_sock = create_tcp_socket();
	
	init_client(&revsrv->clients[0]);
	init_client(&revsrv->clients[1]);

	strncpy(revsrv->bind_ip, bind_ip, sizeof(revsrv->bind_ip));
	revsrv->bind_port = bind_port;

	strncpy(revsrv->http_url, http_url, sizeof(revsrv->http_url));
}

static int
init_http_idler(struct rev_server *revsrv)
{
	/* get empty slot */
	int i;
	for (i = 0; i < MAX_HTTP_IDLERS; i++)
	{
		if (revsrv->http_idlers[i].state == HTTP_IDLER_OFFLINE)
			break;
	}

	ASSERT(i != MAX_HTTP_IDLERS, "reached maximum of http idlers")

	struct http_idler *http_cl = &revsrv->http_idlers[i];
	char protocol[8];
	int port;
	struct hostent *host_addr;
	int res;

	if ((res = parse_url(revsrv->http_url, protocol, http_cl->http_host, &port, http_cl->http_uri)) != 0)
	{
		printf("invalid url '%s' (%d)\n", revsrv->http_url, res);
		return -1;
	}

	//TODO: make this independent of socket family
	memset(&http_cl->addr, 0, sizeof(http_cl->addr));
	http_cl->addr.sin_family = AF_INET;
	http_cl->addr.sin_port = htons(port == -1 ? 80 : port);

	//TODO: host resolving should be threaded
	/* resolve host */
	host_addr = gethostbyname(http_cl->http_host);
	
	if (host_addr == NULL)
	{
		printf("failed to resolve '%s'\n", http_cl->http_host);
		return -2;
	}

	/* build socket */
	http_cl->sock = create_tcp_socket();
	ASSERT(http_cl->sock != -1, "failed to create socket")
	/* nonblock */
	socket_set_block(http_cl->sock, 0);

	/* init connection */
	res = connect(http_cl->sock, (struct sockaddr*)&http_cl->addr, sizeof(http_cl->addr));

	if (!(res == -1 && errno == EINPROGRESS))
	{
		printf("failed to init http connection : %s\n", strerror(errno));
		close(http_cl->sock);
		return -3;
	}

	http_cl->state = HTTP_IDLER_CONNECTING;
	printf("connecting to '%s'\n", http_cl->http_host);

	return i;
}

static void
add_read_fd(struct rev_server *revsrv, int fd)
{
	FD_SET(fd, &revsrv->read_fds);
	if (fd > revsrv->high_desc)
		revsrv->high_desc = fd;
}

static void
add_write_fd(struct rev_server *revsrv, int fd)
{
	FD_SET(fd, &revsrv->write_fds);
	if (fd > revsrv->high_desc)
		revsrv->high_desc = fd;
}

static void
http_idlers_add_fds(struct rev_server *revsrv)
{
	int i;
	/* add polling descriptors */
	for (i = 0; i < MAX_HTTP_IDLERS; i++)
	{
		if (revsrv->http_idlers[i].state == HTTP_IDLER_CONNECTING)
			add_write_fd(revsrv, revsrv->http_idlers[i].sock); /* wait for connection */
		else if (revsrv->http_idlers[i].state == HTTP_IDLER_ONLINE)
			add_read_fd(revsrv, revsrv->http_idlers[i].sock); /* wait for data */
	}
}

static void
handle_http_idlers(struct rev_server *revsrv)
{
	int idlers_online = 0;
	int i;

	for (i = 0; i < MAX_HTTP_IDLERS; i++)
	{
		struct http_idler *cl = &revsrv->http_idlers[i];

		if (cl->state != HTTP_IDLER_OFFLINE)
			idlers_online++;
		
		if (cl->state == HTTP_IDLER_CONNECTING &&
			FD_ISSET(cl->sock, &revsrv->write_fds))
		{
			/* check connection state */
			int res = connect(cl->sock, (struct sockaddr*)&cl->addr, sizeof(&cl->addr));

			if (res == 0)
			{
				printf("connection of %d established\n", i);
				cl->state = HTTP_IDLER_ONLINE;
			}
			else
			{
				printf("Failed to connect : %s\n", strerror(errno));
				close(cl->sock);
				cl->state = HTTP_IDLER_OFFLINE;
			}
		}
	}

	if (!idlers_online)
		init_http_idler(revsrv);
}

static void
reset_fds(struct rev_server *revsrv)
{
	/* reset read/write descriptors */
	revsrv->high_desc = -1;
	FD_ZERO(&revsrv->read_fds);
	FD_ZERO(&revsrv->write_fds);
}

void
revsrv_run(struct rev_server *revsrv)
{
	/* Mainloop */
	while(1)
	{
		printf("tick\n");
		reset_fds(revsrv);
	
		/* add socket descriptors to select */
		http_idlers_add_fds(revsrv);

		/* do select if necessary */
		if (revsrv->high_desc != -1)
		{
			/* select */
			int res;
			if ((res = select(revsrv->high_desc+1, &revsrv->read_fds,
					&revsrv->write_fds, NULL, NULL)) == -1)
			{
				printf("select() failed : %s\n", strerror(errno));
				ASSERT(0, "select failed");
			}
		}
		else
		{
			printf("Warning: no fds for polling\n");
			sleep(1); /* prevent cpu load */
		}
		
		handle_http_idlers(revsrv);
	}
}

int
main(int argc, char **argv)
{
	struct rev_server revsrv;

	#define PHP_URL "http://localhost:8080/sockssrv.php"
	#define BIND_PORT 3443
	#define BIND_IP "127.0.0.1"

	revsrv_init(&revsrv, BIND_IP, BIND_PORT, PHP_URL);

	/* give control to rev server */
	revsrv_run(&revsrv);

	return EXIT_SUCCESS;	
}
