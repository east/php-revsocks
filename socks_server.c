#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stropts.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "socks.h"
#include "socks_server.h"

int
s5srv_init(S5SRV *srv, const char *bind_host, int listening_port, cb_new_connect new_connect, cb_conn_state conn_state, cb_on_data on_data, cb_on_disc on_disc, cb_get_data get_data)
{
	struct sockaddr_in addr;

	srv->new_connect = new_connect;
	srv->conn_state = conn_state;
	srv->on_data = on_data;
	srv->on_disc = on_disc;
	srv->get_data = get_data;

	/* reset clients */
	int i;
	for (i = 0; i < S5SRV_MAX_CLIENTS; i++)
		srv->clients[i].state = S5SRV_CL_OFFLINE;
	srv->num_clients = 0;

	/* build sockaddr */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(listening_port);
	addr.sin_addr.s_addr = inet_addr(bind_host);

	srv->error = 0;
	/* create socket */
	srv->socket = socket(AF_INET, SOCK_STREAM, 0);

	if (srv->socket == -1)
	{
		srv->error = S5SRV_ERROR_CREATING_SOCKET_FAILED;
		return -1;
	}
	
	/* bind socket */
	if (bind(srv->socket, (struct sockaddr*)&addr, sizeof(addr)) == -1)
	{
		srv->error = S5SRV_ERROR_BINDING_SOCKET;
		return -1;
	}

	/* enable listening mode	 */
	if (listen(srv->socket, 3) == -1)
	{	
		srv->error = S5SRV_ERROR_LISTENING_SOCKET;
		return -1;
	}

	/* set non blocking flag */
	unsigned long mode = 1;
	if (ioctl(srv->socket, FIONBIO, &mode) == -1)
	{
		srv->error = S5SRV_ERROR_NON_BLOCK;
		return -1;
	}

	return 0;
}

void
s5srv_close(S5SRV *srv)
{
	int i;

	/* close client sockets */
	for (i = 0; i < S5SRV_MAX_CLIENTS; i++)
	{
		if (srv->clients[i].state != 0)
			close(srv->clients[i].state);
	}

	close(srv->socket);
}

static int
do_select(S5SRV *srv, fd_set *fds, struct timeval *tv)
{
	/* get biggest descriptor */
	int max = srv->socket;

	int i;
	for (i = 0; i < S5SRV_MAX_CLIENTS; i++)
	{
		if (srv->clients[i].state > S5SRV_CL_OFFLINE &&
			srv->clients[i].socket > max)
				max = srv->clients[i].socket;
	}

	/* reset fds */
	FD_ZERO(fds);
	/* add server socket */
	FD_SET(srv->socket, fds);
	/* add client sockets */
	for (i = 0; i < S5SRV_MAX_CLIENTS; i++)
	{
		if (srv->clients[i].state > S5SRV_CL_OFFLINE)
			FD_SET(srv->clients[i].socket, fds);
	}

	return select(max+1, fds, NULL, NULL, tv);
}

static void
add_new_clients(S5SRV *srv)
{
	int i;
	int new_sock;
	struct sockaddr_in addr;
	socklen_t sock_len = sizeof(addr);

	/* try to accept a client */
	new_sock = accept(srv->socket, (struct sockaddr*)&addr, &sock_len);

	if (new_sock == -1)
		return; //TODO: we should check something

	if (sock_len != sizeof(addr))
	{
		close(new_sock);
		return; /* we can't accept this kind of family */
	}	

	S5SRV_CL *new_cl = NULL;

	/* get a free client slot */
	for (i = 0; i < S5SRV_MAX_CLIENTS; i++)
	{
		if (srv->clients[i].state == S5SRV_CL_OFFLINE)
		{
			new_cl = &srv->clients[i];
			break;
		}
	}

	if (!new_cl)
	{
		/* we can't accept more clients */
		printf("I'm full, can't accept moar\n");
		close(new_sock);
		return;
	}

	new_cl->state = S5SRV_CL_ACCEPTED;
	new_cl->socket = new_sock;
	new_cl->addr = addr;

	return; /* we've successfully added a new client */
}

static void
cl_disc(S5SRV_CL *cl)
{
	close(cl->socket);
	cl->state = S5SRV_CL_OFFLINE;
}

static void
cl_handle_packet(S5SRV *srv, S5SRV_CL *cl, char *data, int size)
{
	int i = 0;

	if (cl->state == S5SRV_CL_ACCEPTED)
	{
		/* get method request */
		if (data[i++] != 0x5)
		{
			cl_disc(cl);
			return;
		}

		int num_methods = data[i++];
		
		if (!num_methods || num_methods > size-1)
		{
			/* invalid packet */
			cl_disc(cl);
			return;
		}
		
		int g;
		for (g = 0; g < num_methods; g++)
		{
			if (data[i++] == S5_NO_AUTH_REQUIRED)
				break;
		}

		if (g == num_methods)
		{
			/* decline request */
			cl_disc(cl);
			return;
		}

		/* accept request */
		char d[] = {0x05,  S5_NO_AUTH_REQUIRED};
		send(cl->socket, d, 2, 0);

		cl->state = S5SRV_CL_RECV_REQUEST;
	}
	else if (cl->state == S5SRV_CL_RECV_REQUEST)
	{
		if (data[i++] != 0x5)
		{
			cl_disc(cl);
			return;
		}

		if (size < 5)
		{
			/* packet too small */
			cl_disc(cl);
			return;
		}

		int cmd = data[i++];
		i++; /* reserved */
		int addr_type = data[i++];


		//TODO: accept more cmds/addresses
		if (cmd != S5_CONNECT || addr_type != S5_DOMAIN)
		{
			cl_disc(cl);
			return;
		}

		if (addr_type == S5_DOMAIN)
		{			
			int len = data[i++];
			if (len > size-4 || len > sizeof(cl->req.data)-1)
			{
				/* invalid domain length */
				cl_disc(cl);
				return;
			}

			int min_len = 4 /*0x5,cmd,reserved,addr_type*/ +
							1 /*domain len*/ +
								len /*domain*/ +
									2 /*port*/;
			if (size < min_len)
			{
				/* packet too small */
				cl_disc(cl);
				return;
			}
		
			cl->req.addr_type = S5_DOMAIN;
			memcpy(cl->req.data, data+i, len);
			cl->req.data[len] = '\0';
			i += len;

			cl->req.port = ((unsigned char)data[i++])<<8;
			cl->req.port += (unsigned char)data[i++];

			if (srv->new_connect(cl, &cl->conn_cb_user) == 0)
			{
				/* accept request	 */
				cl->state = S5SRV_CL_CONN_PENDING;
			}
			else
			{
				/* decline request */
				cl_disc(cl);
			}
		}
	}
	else if (cl->state == S5SRV_CL_ONLINE)
		srv->on_data(cl->conn_cb_user, data, size); /* repeat received data */
}

static void
handle_client(S5SRV *srv, fd_set *fds, int id)
{
	char buf[2048];
	S5SRV_CL *cl = &srv->clients[id];

	
	if (FD_ISSET(cl->socket, fds))
	{
		/* we got something */
		int res;

		res = recv(cl->socket, buf, sizeof(buf), 0);

		if (res <= 0)
		{
			if (cl->state == S5SRV_CL_CONN_PENDING || cl->state == S5SRV_CL_ONLINE)
				srv->on_disc(cl->conn_cb_user);
			close(cl->socket);
			cl->state = S5SRV_CL_OFFLINE;
		}
		else
			cl_handle_packet(srv, cl, buf, res);
	}

	if (cl->state == S5SRV_CL_CONN_PENDING)
	{
		int state = srv->conn_state(cl->conn_cb_user);
		if (state == S5SRV_CB_CONN_ONLINE)
		{
			cl->state = S5SRV_CL_ONLINE;
			/* notify about connection suceed */
			char accbuf[] = {0x5, S5_SUCEEDED, 0x0, S5_IPV4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
			send(cl->socket, accbuf, sizeof(accbuf), 0);
		}
		else if (state == S5SRV_CB_CONN_FAILED)
		{
			//TODO: notify client about connection failure
			cl_disc(cl);	
		}
	}
	else if (cl->state == S5SRV_CL_ONLINE)
	{
		/* receive data from server */
		char data[2048];
		int size = sizeof(data);

		srv->get_data(cl->conn_cb_user, data, &size);

		if (size == -1)
		{
			/* server disconnected */
			cl->state = S5SRV_CL_SHUTDOWN;
		}
		else if (size > 0)
			send(cl->socket, data, size, 0);
	}
	else if (cl->state == S5SRV_CL_SHUTDOWN)
	{
		srv->on_disc(cl->conn_cb_user);
		close(cl->socket);
		cl->state = S5SRV_CL_OFFLINE;
	}
}

int
s5srv_tick(S5SRV *srv, int block)
{
	fd_set fds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 1000; //TODO: handle this better

	int res;
	if ((res = do_select(srv, &fds, &tv)) == -1)
		return -1;

	return s5srv_tick_ex(srv, &fds, NULL);
}

int
s5srv_tick_ex(S5SRV *srv, fd_set *rfds, fd_set *wfds)
{
	if (FD_ISSET(srv->socket, rfds))
		add_new_clients(srv);

	/* handle clients */
	int i;
	for (i = 0; i < S5SRV_MAX_CLIENTS; i++)
	{
		if (srv->clients[i].state > S5SRV_CL_OFFLINE)
			handle_client(srv, rfds, i);
	}

	return 0;
}

int
s5srv_get_fds(S5SRV *srv, fd_set *rfds, fd_set *wfds)
{
	int highest_fd;
	
	/* add server socket */
	highest_fd = srv->socket;
	FD_SET(srv->socket, rfds);
	/* add client sockets */
	int i;
	for (i = 0; i < S5SRV_MAX_CLIENTS; i++)
	{
		if (srv->clients[i].state > S5SRV_CL_OFFLINE)
		{
			if (srv->clients[i].socket > highest_fd)
				highest_fd = srv->clients[i].socket;
			FD_SET(srv->clients[i].socket, rfds);
		}
	}

	return highest_fd;
}

void
s5srv_send(S5SRV_CL *cl, char *data, size_t size)
{
	if (size > 0)
		send(cl->socket, data, size, 0);
	else
		cl->state = S5SRV_CL_SHUTDOWN;	
}

