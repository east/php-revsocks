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
#include <assert.h>

#include "system.h"
#include "rev_server.h"
#include "socks.h"
#include "socks_server.h"

enum
{
	CLSTATE_CONNECTING=0,
};

struct cl_hndl
{
	int state;
	int rev_netw_hndl;
};

struct rev_server revsrv;
S5SRV s5srv;

/* when select shouldn't block at all */
int empty_sel_timeout;

int
s5_new_connect(S5SRV_CL *cl, void **user_ptr)
{
	struct cl_hndl *cl_hndl;

	/*
		select has to terminate immediatly
		so rev server is able to handle this connection
	*/
	empty_sel_timeout = 1;

	if (cl->req.addr_type == S5_DOMAIN)
	{
		cl_hndl = malloc(sizeof(struct cl_hndl));
		*user_ptr = cl_hndl;

		cl_hndl->state = CLSTATE_CONNECTING;

		printf("[s5srv] request %s:%d\n", cl->req.data, cl->req.port);

		//TESTING
		struct hostent *host_addr;
		struct netaddr addr;
		host_addr = gethostbyname(cl->req.data);
		ASSERT(host_addr, "failed to resolve host")

		addr.port = cl->req.port;
		addr.type = ADDR_IPV4;
		memcpy(addr.addr_data, host_addr->h_addr_list[0], 4);

		cl_hndl->rev_netw_hndl = revsrv_init_conn(&revsrv, &addr, cl_hndl);
		printf("[s5srv] new rev connection handle %d\n", cl_hndl->rev_netw_hndl);
		return 0;
	}
	else
		printf("[s5srv] request invalid addr type %d\n", cl->req.addr_type);
	return -1;
}

int
s5_conn_state(void *user_ptr)
{
	struct cl_hndl *cl_hndl = user_ptr;
	int state = revsrv_conn_state(&revsrv, cl_hndl->rev_netw_hndl);

	switch(state)
	{
		case REV_CONN_PENDING:
			return S5SRV_CB_CONN_PENDING;
		break;
		case REV_CONN_FAILED:
			return S5SRV_CB_CONN_FAILED;
		break;
		//case REV_CONN_ONLINE:
		default:
			return S5SRV_CB_CONN_ONLINE;
		break;
	}
}

void
s5_on_data(void *user_ptr, char *data, int size)
{
	empty_sel_timeout = 1;
	struct cl_hndl *cl_hndl = user_ptr;
	revsrv_cl_send(&revsrv, cl_hndl->rev_netw_hndl, data, size);
}

void
s5_on_disc(void *user_ptr)
{
	struct cl_hndl *cl_hndl = user_ptr;
	empty_sel_timeout = 1;
	revsrv_cl_close(&revsrv, cl_hndl->rev_netw_hndl);
	printf("[s5srv] disc\n");
}

int
s5_get_data(void *user_ptr, char *data, int size)
{
	struct cl_hndl *cl_hndl = user_ptr;
	return revsrv_cl_recv(&revsrv, cl_hndl->rev_netw_hndl, data, size);
}

int
main (int argc, char **argv)
{
	#define PHP_URL "http://localhost:8080/sockssrv.php"
	#define BIND_PORT 3443
	#define BIND_IP "127.0.0.1"
	#define HTTP_TIMEOUT 60

	if (revsrv_init(&revsrv, BIND_IP, BIND_PORT, PHP_URL, HTTP_TIMEOUT) != 0)
	{
		printf("failed to init rev server\n");
		return EXIT_FAILURE;
	}

	if (s5srv_init(&s5srv, "0.0.0.0", 1080, s5_new_connect,
				s5_conn_state, s5_on_data, s5_on_disc, s5_get_data) != 0)
	{
		printf("failed to init socks5 server\n");
		return EXIT_FAILURE;
	}

	while (1)
	{
		fd_set rfds, wfds;
		int highest_fd = -1, max_block[2], sel_timeout;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		struct timeval tv;
		
		tv.tv_sec = 0;
		tv.tv_usec = 10000;

		max_block[0] = s5srv_max_block_time(&s5srv);
		max_block[1] = revsrv_max_block_time(&revsrv);
		sel_timeout = max(max_block[0], max_block[1]);

		if (sel_timeout != -1)
		{
			printf("own timeout control\n");
			tv.tv_sec = 0;
			tv.tv_usec = sel_timeout;
		}

		if (empty_sel_timeout)
		{
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			/* reset */
			empty_sel_timeout = 0;
		}


		highest_fd = max(revsrv_get_fds(&revsrv, &rfds, &wfds),
							s5srv_get_fds(&s5srv, &rfds, &wfds));
				
		if (highest_fd == -1)
		{
			printf("no desc\n");
			usleep(1000);
		}
		else
			select(highest_fd+1, &rfds, &wfds, NULL, &tv);

		/* handle socks5 server and reverse socks server */
		revsrv_tick(&revsrv, &rfds, &wfds);
		s5srv_tick_ex(&s5srv, &rfds, &wfds);
	}

	s5srv_close(&s5srv);

	return EXIT_SUCCESS;
}
