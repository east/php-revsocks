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

#include "regex_url.h"
#include "protocol.h"
#include "fifobuf.h"

#define PHP_URL "http://localhost:8080/sockssrv.php"
#define BIND_PORT 3443
#define BIND_IP "127.0.0.1"

/*
   this socket is used to send a http request
   which initiates the php script
*/
int http_sock = -1;
/*
   this socket is used to accept the connection
   established by the php script
*/
int rev_listen_sock = -1;
/*
   accepted client socket
*/
int rev_sock = -1;

fd_set pub_fds;
fd_set pub_write_fds;

/* tcp buffers */
struct FIFOBUF rev_in_buf;
struct FIFOBUF rev_out_buf;

static int
create_tcp_socket()
{
   int sock = socket(AF_INET, SOCK_STREAM, 0); 

   if (sock == -1)
   {
	   printf("socket() : %s\n", strerror(errno));
	   return -1;
   }

   /* non-block */
   int state = 1;
   ioctl(sock, FIONBIO, &state);

   return sock;
}

static int
init_sockets()
{
   http_sock = create_tcp_socket(); 
   
   if (http_sock == -1)
	   return -1;
   
   rev_listen_sock = create_tcp_socket();
   /* make this socket reusable */
   struct linger li;

   li.l_onoff = 1;
   li.l_linger = 0;
   setsockopt(rev_listen_sock, SOL_SOCKET, SO_LINGER,
			   &li, sizeof(struct linger));

   if (rev_listen_sock == -1)
	   return -1;

   /* bind listening socket */
   struct sockaddr_in addr;

   memset(&addr, 0, sizeof(struct sockaddr_in));
   addr.sin_addr.s_addr = inet_addr(BIND_IP);
   addr.sin_port = htons(BIND_PORT);
   addr.sin_family = AF_INET;

   if (bind(rev_listen_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
   {
	   printf("bind() : %s\n", strerror(errno));
	   goto err_cls_socks;
   }

   /* listening mode */
   if (listen(rev_listen_sock, 1) != 0)
   {
	   printf("listen() : %s\n", strerror(errno));
	   goto err_cls_socks;
   }

   printf("listening on %s:%d\n", BIND_IP, BIND_PORT);

   return 0;
err_cls_socks:
   close(http_sock);
   close(rev_listen_sock);
   return -1;
}

/* doing select readfds/writefds on socket */
static void
wait_socket(int s, int ms, int writefds)
{
   struct timeval tv;
   fd_set fds;

   FD_ZERO(&fds);
   FD_SET(s, &fds);

   tv.tv_sec = 0;
   tv.tv_usec = ms*1000;

   if (select(s+1, !writefds?&fds:NULL, writefds?&fds:NULL, NULL, ms == -1 ? NULL : &tv) == -1)
   {
	   printf("select() failed : %s\n", strerror(errno));
	   assert(0);		
   }
}

static int
init_http(int s, const char *url)
{
   char protocol[8];	
   char host[128];	
   char uri[256];
   int port;
   struct sockaddr_in addr;
   struct hostent *host_addr;
   int res;
   char buf[2048];

   if ((res = parse_url(url, protocol, host, &port, uri)) != 0)
   {
	   printf("invalid url '%s' (%d)\n", url, res);
	   return -1;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port == -1 ? 80 : port);

   /* resolve host */
   host_addr = gethostbyname(host);

   if (host_addr == NULL)
   {
	   printf("failed to resolve '%s'\n", host);
	   return -2;
   }

   memcpy(&addr.sin_addr, host_addr->h_addr, 4);	

   /* try to connect */	
   res = connect(http_sock, (struct sockaddr*)&addr, sizeof(addr));

   if (res != -1 || errno != EINPROGRESS)
   {
	   printf("connecting failed: %d : %s\n", res, strerror(errno));
	   return -3;
   }

   wait_socket(http_sock, -1, 1);	

   /* check connection */
   res = connect(http_sock, (struct sockaddr*)&addr, sizeof(addr));
   
   if (res != 0)
   {
	   printf("connecting failed: %d : %s\n", res, strerror(errno));
	   return -4;
   }

   /* build request */
   if (port == -1)
	   snprintf(buf, sizeof(buf), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", uri, host);
   else
	   snprintf(buf, sizeof(buf), "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", uri, host, port);
   
   /* send request */
   send(http_sock, buf, strlen(buf), 0);

   return 0;
}

static int
max(int x, int y)
{
   if (x > y)
	   return x;
   return y;
}

static int
accept_client()
{
   if (!FD_ISSET(rev_listen_sock, &pub_fds))
	   return -1;

   struct sockaddr_in addr;
   socklen_t addr_size = sizeof(addr);

   int res = accept(rev_listen_sock, (struct sockaddr*)&addr, &addr_size);

   if (res == -1)
   {
	   printf("error accept() : %s\n", strerror(errno));
	   return -1;
   }
   
   rev_sock = res;
   return 0;
}

static int
http_alive()
{
   char buf[2048];

   if (!FD_ISSET(http_sock, &pub_fds))
	   return 1; /* still alive */
   
   int res = recv(http_sock, buf, sizeof(buf), 0);

   if (res == 0)
   {
	   printf("http endpoint closed the connection\n");
	   return 0;
   }
   else if(res == -1)
   {
	   printf("http sock error : %s\n", strerror(errno));
	   return 0;
   }
   else
	   printf("Warning: http endpoint returned data (%d bytes)\n", res);

   return 1;
}

static void
rev_send_data()
{
	if (fifo_len(&rev_out_buf) == 0)
		return;

	int res = send(rev_sock, rev_out_buf.data, fifo_len(&rev_out_buf), 0);

	if (res == -1)
	{
		printf("Failed to send data : %s\n", strerror(errno));
		return;
	}
	else if(res > 0)
	{
		/* skip sent bytes */
		fifo_read(&rev_out_buf, NULL, res);
		printf("%d bytes sent\n", res);
	}
}

static void
handle_in_data()
{
	int buf_size = fifo_len(&rev_in_buf);

	if (buf_size < 3)
		return;
	
	int msg_size = *((uint16_t*)rev_in_buf.data);
	int msg_id = *((uint8_t*)(rev_in_buf.data+2));

	if (buf_size < 3 + msg_size)
		return; /* not enough data */

	/* skip size & id */
	fifo_read(&rev_in_buf, NULL, 3);

	if (msg_id == MSG_DBGMSG)
	{
		printf("got dbgmsg: %s\n", rev_in_buf.data);
		fifo_read(&rev_in_buf, NULL, msg_size);
	}
	else if(msg_id == MSG_PONG)
		printf("got pong\n");
	else
		/* skip message */
		fifo_read(&rev_in_buf, NULL, msg_size);

	printf("got message of type %d len %d\n", msg_id, msg_size);
}

static int
handle_tunnel()
{
	/* clear out buffer */
	rev_send_data();

	if (!FD_ISSET(rev_sock, &pub_fds))
		return 0;
   
	char buf[2048];
	int res = recv(rev_sock, buf, sizeof(buf), 0);

	if (res == 0)
	{
		printf("tunnel connection has been closed\n");
		return -1;
	}
	else if (res == -1)
	{
		printf("rev sock error : %s\n", strerror(errno));
		return -1;
	}
	else
	{
		printf("got %d bytes from rev socket\n", res);
		
		if (fifo_write(&rev_in_buf, buf, res) != 0)
		{
			printf("tcp buffer overflow (in)\n");
			assert(0);
		}
		
		handle_in_data();
	}

	return 0;
}

static void
rev_send_msg(int msg_id, const char *data, size_t size)
{
	uint16_t size_16 = size;
	uint8_t msg_id_8 = msg_id;

	if (fifo_space_left(&rev_out_buf) < 3 + size)
	{
		printf("tcp buffer overflow (out)\n");
		assert(0);
	}

	/* put size */
	fifo_write(&rev_out_buf, (const char*)&size_16, 2);	
	/* msg id */
	fifo_write(&rev_out_buf, (const char*)&msg_id_8, 1);
	/* data */
	fifo_write(&rev_out_buf, (const char*)data, size);
}

static void
rev_send_dbg(const char *dbgmsg)
{
	int len = strlen(dbgmsg);
	rev_send_msg(MSG_DBGMSG, dbgmsg, len+1);
}

enum
{
	AWAITING_REV=0,
	REV_CONNECTED,
	REV_INTERRUPTED,
};

int
main(int argc, char **argv)
{
	char http_url[256];
	int state;

	if (init_sockets() != 0)
		return EXIT_FAILURE;

	snprintf(http_url, sizeof(http_url), "%s?ip=%s&port=%d", PHP_URL, BIND_IP, BIND_PORT);

	if (init_http(http_sock, http_url) == 0)
		printf("http request pending...\n");
	else
	{
		printf("http session could not be initiated\n");
		close(http_sock);
		return EXIT_FAILURE;
	}

	/* init tcp buffers */
	fifo_init(&rev_out_buf, malloc(BUF_SIZE), BUF_SIZE);
	fifo_init(&rev_in_buf, malloc(BUF_SIZE), BUF_SIZE);
	
	/* wait for connection on ref socket */
	state = AWAITING_REV;
	while (1)
	{
		int high_desc = -1;

		FD_ZERO(&pub_fds);
		FD_ZERO(&pub_write_fds);
		
		FD_SET(http_sock, &pub_fds);
		high_desc = http_sock;
	
		if (state == AWAITING_REV)
		{
			FD_SET(rev_listen_sock, &pub_fds);
			high_desc = max(high_desc, rev_listen_sock);
		}
		else if(state == REV_CONNECTED)
		{
			FD_SET(rev_sock, &pub_fds);

			if (fifo_len(&rev_out_buf) > 0)
				/* we want to send data */
				FD_SET(rev_sock, &pub_write_fds);
			
			high_desc = max(high_desc, rev_sock);
		}

		select(high_desc+1, &pub_fds, &pub_write_fds, NULL, NULL);

		if (!http_alive())
		{
			/*
				the http server closed the connection
				(hopefully it's a cgi timeout)
			*/

			close(http_sock);

			if (state == REV_CONNECTED)
			{
				close(rev_sock);
				rev_sock = -1;
			}

			break;
		}

		if (state == AWAITING_REV)
		{
			if (accept_client() == 0)
			{
				/* php socks ack */
				state = REV_CONNECTED;
				printf("php sockssrv acknowledged! tunnel initiated\n");
				
				//TESTING
				rev_send_msg(MSG_PING, NULL, 0);
			}
		}
		else if (state == REV_CONNECTED)
		{
			if (handle_tunnel() != 0)
			{
				/* connection closed */
				close(rev_sock);
				rev_sock = -1;
				state = REV_INTERRUPTED;
			}
		}

		printf("tick\n");
	}

	/* free */
	free(rev_in_buf.data);
	free(rev_out_buf.data);

	return EXIT_SUCCESS;
}
