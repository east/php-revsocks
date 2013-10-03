#ifndef PROTOCOL_H
#define PROTOCOL_H

enum
{
	NETWORK_BUF_SIZE=4096*4,
	TMP_BUF_SIZE=4096,
	MAX_MSG_SIZE=4096,

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
	MSG_SEND, /* <uint16 id>, <uint16 size>, <data> */
	MSG_EXIT,

	/* connection states */
	CONN_STATE_ONLINE=0,
	CONN_STATE_ERROR, /* connection lost / failed to connect */
	CONN_STATE_CONNECTING,
	CONN_STATE_OFFLINE,
};

#endif
