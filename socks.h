#ifndef SOCKS_H
#define SOCKS_H

enum
{
	/* socks versions */
	SOCKS_VER_5=0x05,

	/* socks5 methods */
	S5_NO_AUTH_REQUIRED=0x00,
	S5_GSSAPI=0x01,
	S5_USER_PASS=0x02,
	S5_NO_ACCEPTED=0xFF,

	/* socks5 cmds */
	S5_CONNECT=0x01,
	S5_BIND=0x02,
	S5_UDP=0x03,

	/* socks5 address types */
	S5_IPV4=0x01,
	S5_DOMAIN=0x03,
	S5_IPV6=0x04,

	/* socks5 replies */
	S5_SUCEEDED=0x00,
	S5_NOT_ALLOWED,
	S5_NET_UNREACHABLE,
	S5_HOST_UNREACHABLE,
	S5_CONN_REFUSED,
	S5_TTL_EXPIRED,
	S5_CMD_NOT_SUPPORTED,
	S5_ATYPE_NOT_SUPPORTED,
	
	S5_RECV_FAILED,
	S5_PROTOCOL_ERROR,
	S5_METHODS_REFUSED,

	/* socks5 states */
	S5_RECV_METHOD=0, /* receive method + send connection details */
	S5_RECV_CONNECTION, /* receive connection details */
	S5_ONLINE,

	/* socks tick states */
	SOCKS_PENDING=0, /* processing connection */
	SOCKS_ONLINE, /* the connection has been established */
	SOCKS_FAILED, /* failed to connect, SOCKS_HANDLER::error contains the error */
};

typedef void (*socks_send_cb)(void *userptr, void *data, int size);
typedef int (*socks_recv_cb)(void *userptr, void *out, int maxsize);

typedef struct
{
	int state;
	int version;
	
	int error; /* will be set on error */
	int cmd; /* connect, bind, udp */
	
	char username[64];
	char password[64];

	void *userptr; /* could contain socket informtions for callback */
	int addr_type; /* ipv4, ipv6, domain */
	char addr[64];
	int port;

	/* send/recv callbacks */
	socks_send_cb send_cb;
	socks_recv_cb recv_cb;
} SOCKS_HANDLER;

/* function: s5_init_v4conn */
/* inits SOCKS_HANDLER for a socks5 ipv4 connection */
/* parameters: */
/* socks		-	pointer to SOCKS_HANDLER */
/* addr		-	pointer to 4 byte array (ipv4 destination address) */
/* port		-	destination port */
/* send_cb	-	send callback */
/* recv_cb	-	receive callback */
/* userptr	-	userpointer for callbacks */
void s5_init_v4conn(SOCKS_HANDLER *socks, const char *addr, int port, socks_send_cb send_cb, socks_recv_cb recv_cb, void *userptr);

/* function: s5_init_domconn */
/* inits SOCKS_HANDLER fpr a socks5 connecting using domains */
/* parameters: */
/* socks		-	pointer to SOCKS_HANDLER */
/* domain	-	pointer to string which contains the domainname */
/* port		-	destination port */
/* send_cb	-	send callback */
/* recv_cb	-	receive callback */
/* userptr	-	userpointer for callbacks */

void s5_init_domconn(SOCKS_HANDLER *socks, const char *domain, int port, socks_send_cb send_cb, socks_recv_cb recv_cb, void *userptr);

/* function: socks_tick */
/* ticks the connection init */
/* used after SOCKS_HANDLER init */
/* parameters: socks		-	pointer to SOCKS_HANDLER */
int socks_tick(SOCKS_HANDLER *socks);

#endif
