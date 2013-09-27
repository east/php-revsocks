#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "socks.h"

static void
do_request(SOCKS_HANDLER *socks)
{
	/* build request packet */
	char buf[16];
	int i = 0;
	
	buf[i++] = 0x05; /* socks version 5 */

	if (socks->username[0] || socks->password[0])
	{
		//TODO: implement socks5 auth
	}
	else
	{
		buf[i++] = 1; /* number of methods */
		buf[i++] = S5_NO_AUTH_REQUIRED; /* set first method */
	}

	/* send packet */
	socks->send_cb(socks->userptr, buf, i);
	socks->state = S5_RECV_METHOD;
}

void
s5_init_v4conn(SOCKS_HANDLER *socks, const char *addr, int port, socks_send_cb send_cb, socks_recv_cb recv_cb, void *userptr)
{
	socks->version = SOCKS_VER_5;
	socks->addr_type = S5_IPV4;	
	memcpy(socks->addr, addr, 4);
	socks->port = port;
	socks->cmd = S5_CONNECT;
	socks->send_cb = send_cb;
	socks->recv_cb = recv_cb;
	socks->userptr = userptr;

	/* reset auth */
	socks->username[0] = 0;
	socks->password[0] = 0;

	do_request(socks);
}

void
s5_init_domconn(SOCKS_HANDLER *socks, const char *domain, int port, socks_send_cb send_cb, socks_recv_cb recv_cb, void *userptr)
{
	socks->version = SOCKS_VER_5;
	socks->addr_type = S5_DOMAIN;	
	strncpy(socks->addr, domain, sizeof(socks->addr));
	socks->port = port;
	socks->cmd = S5_CONNECT;
	socks->send_cb = send_cb;
	socks->recv_cb = recv_cb;
	socks->userptr = userptr;

	/* reset auth */
	socks->username[0] = 0;
	socks->password[0] = 0;

	do_request(socks);
}


void
socks_send_conn_details(SOCKS_HANDLER *socks)
{
	char buf[256];
	int i = 0;
	
	if (socks->version == SOCKS_VER_5)
	{
		buf[i++] = 0x05; /* socks5 version */
		buf[i++] = socks->cmd; /* command */
		buf[i++] = 0; /* reserved */
		buf[i++] = socks->addr_type; /* address type */

		/* add address */
		if (socks->addr_type == S5_IPV4)
		{
			buf[i++] = socks->addr[0];
			buf[i++] = socks->addr[1];
			buf[i++] = socks->addr[2];
			buf[i++] = socks->addr[3];
		}
		else if (socks->addr_type == S5_DOMAIN)
		{
			int len = strlen(socks->addr);
			buf[i++] = len; //add domain length
			memcpy(buf+i, socks->addr, len);
			i += len;
		}
		//TODO: support more address types/familys

		/* add port */
		buf[i++] = (socks->port&0xFFFF)>>8;
		buf[i++] = socks->port&0xFF;
	}

	/* send packet */
	socks->send_cb(socks->userptr, buf, i);
}

int
socks_tick(SOCKS_HANDLER *socks)
{
	char buf[256];
	
	if (socks->version == SOCKS_VER_5)
	{
		if (socks->state == S5_RECV_METHOD)
		{
			/* receive packet (byte version, byte accepted_method) */
			int bytes = socks->recv_cb(socks->userptr, buf, sizeof(buf));

			if (bytes <= 0)
			{
				socks->error = S5_RECV_FAILED;
				return SOCKS_FAILED;
			}

			if (bytes != 2 || buf[0] != 0x05)
			{
				socks->error = S5_PROTOCOL_ERROR;
				return SOCKS_FAILED;
			}

			if (buf[1] == 0xFF)
			{
				/* no method has been accepted */
				socks->error = S5_METHODS_REFUSED;
				return SOCKS_FAILED;
			}
			else
			{
				/* method has been accepted */
				socks_send_conn_details(socks);
				socks->state = S5_RECV_CONNECTION;
				return SOCKS_PENDING; /* one step left */
			}
		}
		else if (socks->state == S5_RECV_CONNECTION)
		{
			/* receive packet (version, result, reserved, addrtype, addr, port) */
			int bytes = socks->recv_cb(socks->userptr, buf, sizeof(buf));

			if (bytes <= 0)
			{
				socks->error = S5_RECV_FAILED;
				return SOCKS_FAILED;
			}

			if (buf[0] != 0x05 || bytes < 4)
			{
				socks->error = S5_PROTOCOL_ERROR;
				return SOCKS_FAILED;
			}

			if (buf[1] == S5_SUCEEDED)
			{
				/* connection online */
				socks->state = S5_ONLINE;
				return SOCKS_ONLINE;
			}
			else
			{
				socks->error = buf[1];
				return SOCKS_FAILED;
			}
		}
	}

	/* invalid socks type */
	return SOCKS_FAILED;
}
