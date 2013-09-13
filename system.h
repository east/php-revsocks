#ifndef SYSTEM_H
#define SYSTEM_H

#define ASSERT(test, info) if (!(test)) { *((char*)NULL) = 0; }

enum
{
	/* address types */
	ADDR_IPV4=0, /* <uint32> */
	ADDR_IPV6, /* <uint128> */
	ADDR_DOMAIN, /* <str> */
};

struct netaddr
{
	int type;
	char addr_data[256];
	int port;
};

int create_tcp_socket();
void socket_set_block(int sock, int block);
void socket_set_linger(int sock);
void netaddr_init_ipv4(struct netaddr *addr, const char *ip, int port);

#endif
