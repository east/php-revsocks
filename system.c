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

int create_tcp_socket()
{
   int sock = socket(AF_INET, SOCK_STREAM, 0); 

   if (sock == -1)
   {
	   printf("socket() : %s\n", strerror(errno));
	   return -1;
   }

   return sock;
}


void socket_set_block(int sock, int block)
{
	block = !block;
	ioctl(sock, FIONBIO, &block);
}

