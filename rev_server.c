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
#include <time.h>

#include "system.h"
#include "regex_url.h"
#include "rev_network.h"
#include "rev_server.h"

static void
init_client(struct rev_client *cl)
{
	cl->sock = -1;

	fifo_init(&cl->rev_in_buf, malloc(TCP_BUF_SIZE), TCP_BUF_SIZE);
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
	socket_set_linger(revsrv->rev_listen_sock);

	init_client(&revsrv->clients[0]);
	init_client(&revsrv->clients[1]);

	revsrv->usable_cl = NULL;

	strncpy(revsrv->bind_ip, bind_ip, sizeof(revsrv->bind_ip));
	revsrv->bind_port = bind_port;

	strncpy(revsrv->http_url, http_url, sizeof(revsrv->http_url));

	revsrv->new_idler_date = 0;
	revsrv->last_idler_lifetime = -1;

	/* reset tcp connection id pool */
	int i;
	for (i = 0; i < MAX_NETWORK_HANDLES; i++)
	{
		revsrv->netw_hndls[i].id = i;
		revsrv->netw_hndls[i].state = NETW_HNDL_OFFLINE;
	}
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
	char buf[256];
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

	/* add port to http host if necessary */
	if (port != -1)
	{
		snprintf(buf, sizeof(buf), ":%d", port);
		strncat(http_cl->http_host, buf, sizeof(http_cl->http_host));
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
	time_t now = time(NULL);

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

				/* send request */
				char buf[1024];
				int len = snprintf(buf, sizeof(buf),
									"GET %s?ip=%s&port=%d HTTP/1.1\r\n"
									"Host: %s\r\n"
									"Connection: close\r\n"
									"\r\n",
									cl->http_uri, revsrv->bind_ip,
									revsrv->bind_port, cl->http_host);
				send(cl->sock, buf, len, 0);

				cl->state = HTTP_IDLER_ONLINE;
				cl->date = now;
			}
			else
			{
				printf("Failed to connect : %s\n", strerror(errno));
				close(cl->sock);
				cl->state = HTTP_IDLER_OFFLINE;
			}
		}
		else if(cl->state == HTTP_IDLER_ONLINE && FD_ISSET(cl->sock, &revsrv->read_fds))
		{
			/* receive data */
			char buf[2048];
			int res = recv(cl->sock, buf, sizeof(buf), 0);

			if (res <= 0)
			{
				if (res == 0)
					printf("http idler %d disconnected (%d seconds online)\n", i, (int)(now-cl->date));
				else
					printf("http idler error recv() : %s\n", strerror(errno));

				close(cl->sock);
				cl->state = HTTP_IDLER_OFFLINE;

				revsrv->last_idler_lifetime = (int)(now-cl->date);
			}
		}
	}

	if ((revsrv->new_idler_date != -1 && revsrv->new_idler_date <= now) ||
			idlers_online == 0)
	{
		init_http_idler(revsrv);
		revsrv->new_idler_date = /*-1*/ now+10;

	//	if (revsrv->last_idler_lifetime > 0)
	//		revsrv->new_idler_date = now+revsrv->last_idler_lifetime/2;
	}
}

static void
reset_fds(struct rev_server *revsrv)
{
	/* reset read/write descriptors */
	revsrv->high_desc = -1;
	FD_ZERO(&revsrv->read_fds);
	FD_ZERO(&revsrv->write_fds);
}

static void
on_netw_hndl_disc(struct rev_server *revsrv, int id)
{
	printf("netw handle %d disc\n", id);
}

static void
on_rev_disc(struct rev_server *revsrv, struct rev_client *cl)
{
	/* care of network handles opened by this client */
	int i;
	for (i = 0; i < MAX_NETWORK_HANDLES; i++)
	{
		struct network_handle *hndl = &revsrv->netw_hndls[i];

		if (hndl->cl == cl)
		{
			hndl->state = NETW_HNDL_OFFLINE;
			on_netw_hndl_disc(revsrv, hndl);
		}
	}

}

static void
handle_rev_client(struct rev_server *revsrv, struct rev_client *cl)
{
	int disc_client = 0;

	if (FD_ISSET(cl->sock, &revsrv->read_fds))
	{
		char buf[2048];
		/* receive data */
		int res = recv(cl->sock, buf, sizeof(buf), 0);

		if (res <= 0)
		{
			if (res == 0)
				printf("rev client disconnected\n");
			else
				printf("rev client recv() : %s\n", strerror(errno));

			close(cl->sock);
			cl->sock = -1;
			on_rev_disc(revsrv, cl);
		}
		else
		{
			/* store data */
			if (fifo_write(&cl->rev_in_buf, buf, res) != 0)
			{
				printf("rev client tcp in buffer is full\n");
				ASSERT(0, "tcp in buf overflow")
			}

			printf("stored %d bytes\n", res);
		}
	}

	/* network handler */
	rev_pump_network(revsrv, cl);

	/* send queued data */
	int to_send = fifo_len(&cl->rev_out_buf);
	if (to_send > 0)
	{
		int bytes = send(cl->sock, cl->rev_out_buf.data, to_send, 0);

		if (bytes == -1)
		{
			printf("rev cl send() : %s\n", strerror(errno));
			disc_client = 1;
		}
		else
			fifo_read(&cl->rev_out_buf, NULL, bytes);

		printf("cl %d bytes sent\n", bytes);
	}


	if (disc_client)
	{
		if (revsrv->usable_cl == cl)
			revsrv->usable_cl = NULL;
		close(cl->sock);
	}
}

void
revsrv_run(struct rev_server *revsrv)
{
	/* bind listening socket */
	//TODO: do this socket family independent
	struct sockaddr_in addr;
	int i;

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_addr.s_addr = inet_addr(revsrv->bind_ip);
	addr.sin_port = htons(revsrv->bind_port);
	addr.sin_family = AF_INET;
	
	if (bind(revsrv->rev_listen_sock,
		(struct sockaddr*)&addr, sizeof(addr)) != 0)
	{
		printf("bind() : %s\n", strerror(errno));
		return;
	}

	/* listening mode */
	if (listen(revsrv->rev_listen_sock, 2) != 0)
	{
		printf("listen() : %s\n", strerror(errno));
		return;
	}

	printf("listening on %s:%d\n", revsrv->bind_ip, revsrv->bind_port);
	
	/* Mainloop */
	while(1)
	{
		reset_fds(revsrv);
	
		/* add socket descriptors to select */
		http_idlers_add_fds(revsrv);
		add_read_fd(revsrv, revsrv->rev_listen_sock);

		for (i = 1; i >= 0; i--)
		{
			if (revsrv->clients[i].sock == -1)
				continue;
			
			add_read_fd(revsrv, revsrv->clients[i].sock);

			/* if we need to send data select() should break when we are able to do it */
			if (fifo_len(&revsrv->clients[i].rev_out_buf) > 0)
				add_write_fd(revsrv, revsrv->clients[i].sock);
		}
		
		/* do select if necessary */
		if (revsrv->high_desc != -1)
		{
			/* select */
			int res;
			struct timeval tv;

			/* select timeout after one second */
			tv.tv_sec = 1;
			tv.tv_usec = 0;

			if ((res = select(revsrv->high_desc+1, &revsrv->read_fds,
					&revsrv->write_fds, NULL, &tv)) == -1)
			{
				printf("select() failed : %s\n", strerror(errno));
				ASSERT(0, "select failed");
			}
		}
		else
			usleep(1000); /* prevent cpu load */
		
		handle_http_idlers(revsrv);

		if (FD_ISSET(revsrv->rev_listen_sock,
			&revsrv->read_fds))
		{
			struct rev_client *cl = NULL;
			int tmp_sock;

			if (revsrv->clients[1].sock == -1)
				cl = &revsrv->clients[1];
			if (revsrv->clients[0].sock == -1)
				cl = &revsrv->clients[0];

			tmp_sock = accept(revsrv->rev_listen_sock, NULL, NULL);
			ASSERT(tmp_sock != -1, "accept() returned -1")

			if (!cl)
			{
				printf("Warning: can't accept more rev clients\n");
				close(tmp_sock);
			}
			else
			{
				/* add client */
				cl->sock = tmp_sock;

				/* empty tcp buffers */
				fifo_clean(&cl->rev_in_buf);
				fifo_clean(&cl->rev_out_buf);

				//TODO: do this more smart
				revsrv->usable_cl = cl;

				printf("rev client accepted\n");

				//const char lo_ip[] = {127, 0, 0, 1};
				//int id = rev_init_conn(revsrv, ADDR_IPV4, lo_ip, 8080);
				//printf("NEW conn id %d\n", id);
			}
		}

		/* rev clients */
		if (revsrv->clients[0].sock != -1)
			handle_rev_client(revsrv, &revsrv->clients[0]);
		if (revsrv->clients[1].sock != -1)
			handle_rev_client(revsrv, &revsrv->clients[1]);

		/* handle network handles */
		for (i = 0; i < MAX_NETWORK_HANDLES; i++)
		{
			struct network_handle *hndl = &revsrv->netw_hndls[i];
			if (hndl->state == NETW_HNDL_OFFLINE)
				continue;

			if (hndl->state == NETW_HNDL_TCP_INIT_CONNECT)
			{
				hndl->cl = revsrv_usable_cl(revsrv);

				if (!hndl->cl)
				{
					printf("Can't init connection (no cl online)\n");
					continue;
				}

				/* send tcp initiation */
				struct netmsg msg;
				char buf[256];

				msg.id = MSG_CONNECT;
				msg.data = buf;

				if (hndl->dst_addr.type == ADDR_IPV4)
				{
					msg.size = 9;
					
					*((uint16_t*)buf) = i;
					*((uint16_t*)(buf+2)) = ADDR_IPV4;
					*((uint32_t*)(buf+3)) = *((uint32_t*)hndl->dst_addr.addr_data);
					*((uint16_t*)(buf+7)) = hndl->dst_addr.port;					
				}
				else //TODO: implement all addr types
				{ ASSERT(0, "can't handle non ipv4") }

				rev_send_msg(revsrv, hndl->cl, &msg);
				hndl->state = NETW_HNDL_TCP_CONNECT;
			}
			else if(hndl->state == NETW_HNDL_TCP_FAIL ||
				hndl->state == NETW_HNDL_TCP_DISC)
			{
				hndl->state = NETW_HNDL_OFFLINE;
				on_netw_hndl_disc(revsrv, i);
			}
		}
	}
}

struct rev_client*
revsrv_usable_cl(struct rev_server *revsrv)
{
	if (!revsrv->usable_cl)
		printf("Warning: no usable cl selected\n");
	return revsrv->usable_cl;
}

int revsrv_new_netw_hndl(struct rev_server *revsrv)
{
	int i;
	for (i = 0; i < MAX_NETWORK_HANDLES; i++) {
		if (revsrv->netw_hndls[i].state == NETW_HNDL_OFFLINE)
			break;
	}

	if (i == MAX_NETWORK_HANDLES)
	{
		printf("Warning: network handle pool full");
		return -1;
	}

	return i;
}

void revsrv_free_netw_hndl(struct rev_server *revsrv, int id)
{
	ASSERT(id >= 0 && id <= MAX_NETWORK_HANDLES, "invalid network handle id")
	revsrv->netw_hndls[id].state = NETW_HNDL_OFFLINE;
}

struct network_handle*
revsrv_netw_hndl(struct rev_server *revsrv, int id)
{
	ASSERT(id >= 0 && id <= MAX_NETWORK_HANDLES, "invalid network handle")
	return &revsrv->netw_hndls[id];
}

int
revsrv_init_conn(struct rev_server *revsrv, struct netaddr *addr)
{
	int netw_hndl;

	netw_hndl = revsrv_new_netw_hndl(revsrv);

	if (netw_hndl == -1)
		return -1;

	struct network_handle *hndl = revsrv_netw_hndl(revsrv, netw_hndl);

	hndl->state = NETW_HNDL_TCP_INIT_CONNECT;
	hndl->cl = NULL;
	memcpy(&hndl->dst_addr, addr, sizeof(hndl->dst_addr));

	return 0;
}

int
main(int argc, char **argv)
{
	struct rev_server revsrv;

	#define PHP_URL "http://localhost:8080/sockssrv.php"
	#define BIND_PORT 3443
	#define BIND_IP "127.0.0.1"

	revsrv_init(&revsrv, BIND_IP, BIND_PORT, PHP_URL);

	//TESTING
	struct netaddr addr;
	netaddr_init_ipv4(&addr, "127.0.0.1", 3333);
	revsrv_init_conn(&revsrv, &addr);

	/* give control to rev server */
	revsrv_run(&revsrv);

	return EXIT_SUCCESS;	
}

