#ifndef PROTOCOL_H
#define PROTOCOL_H

enum
{
	BUF_SIZE=4096,

	// msg types
	MSG_DBGMSG=0, /* <str> */
	MSG_PING,
	MSG_PONG,
	/*
		initiate a new tcp connection
		<uint16 id>, <uint8 addr type>, <addr>, <uint16 port>
	*/
	MSG_CONNECT,
	/* tcp connection state	*/
	MSG_CONN_STATE, /* <uint16 id>, <uint8 state>, <str error string> */

	/* address types */
	ADDR_IPV4=0, /* <uint32> */
	ADDR_IPV6, /* <uint128> */
	ADDR_DOMAIN, /* <str> */

	/* connection states */
	CONN_STATE_ONLINE=0,
	CONN_STATE_ERROR,
	CONN_STATE_CONNECTING,
	CONN_STATE_OFFLINE,
};

#endif
