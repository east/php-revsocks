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

int
main (int argc, char **argv)
{
	struct rev_server revsrv;

	#define PHP_URL "http://localhost:8080/sockssrv.php"
	#define BIND_PORT 3443
	#define BIND_IP "127.0.0.1"
	#define HTTP_TIMEOUT 60

	revsrv_init(&revsrv, BIND_IP, BIND_PORT, PHP_URL, HTTP_TIMEOUT);

	//TESTING
	struct netaddr addr;
	netaddr_init_ipv4(&addr, "127.0.0.1", 2222);
	int netwhndl = revsrv_init_conn(&revsrv, &addr);
	revsrv_send(&revsrv, netwhndl, "hallo\n", 6);

	while (1)
	{
		fd_set rfds, wfds;
		int highest_fd = -1;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		struct timeval tv;
		
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		highest_fd = revsrv_get_fds(&revsrv, &rfds, &wfds);
		
		if (highest_fd == -1)
		{
			printf("no desc\n");
			usleep(1000);
		}
		else
			select(highest_fd+1, &rfds, &wfds, NULL, &tv);
		revsrv_tick(&revsrv, &rfds, &wfds);
	}

	return EXIT_SUCCESS;
}
