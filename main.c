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

#define PHP_URL "http://localhost:8080/"
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
int rev_sock = -1;

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
	
	rev_sock = create_tcp_socket();

	if (rev_sock == -1)
		return -1;

	/* bind listening socket */
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_addr.s_addr = inet_addr(BIND_IP);
	addr.sin_port = htons(BIND_PORT);
	addr.sin_family = AF_INET;

	if (bind(rev_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
	{
		printf("bind() : %s\n", strerror(errno));
		goto err_cls_socks;
	}

	/* listening mode */
	if (listen(rev_sock, 1) != 0)
	{
		printf("listen() : %s\n", strerror(errno));
		goto err_cls_socks;
	}

	printf("listening on %s:%d\n", BIND_IP, BIND_PORT);

	return 0;
err_cls_socks:
	close(http_sock);
	close(rev_sock);
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
		snprintf(buf, sizeof(buf), "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", uri, host);
	else
		snprintf(buf, sizeof(buf), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n", uri, host, port);
	
	/* send request */
	send(http_sock, buf, strlen(buf), 0);

	return 0;
}

int
main(int argc, char **argv)
{
	if (init_sockets() != 0)
		return EXIT_FAILURE;

	if (init_http(http_sock, PHP_URL) == 0)
		printf("http request pending...\n");

	return EXIT_SUCCESS;
}
