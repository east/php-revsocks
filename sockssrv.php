<?php
	require("buffer.php");

	function dbg_log($str)
	{
		$file = fopen("phpsocks.log", "a+");		
		fwrite($file, $str."\n");
		fclose($file);
	}

	function handle_data()
	{
		global $tcp_in_buf, $sessions;

		$len = mb_strlen($tcp_in_buf);

		if ($len < 3)
			return false;

		$msg_size = buf_get_uint16($tcp_in_buf, false);
		$msg_id = buf_get_uint8(substr($tcp_in_buf, 2), false);
		dbg_log("msg ".$msg_id." len ".$msg_size);

		if (mb_strlen($tcp_in_buf) < 3 + $msg_size)
			return false; // not enough data

		buf_skip($tcp_in_buf, 3);

		if ($msg_id == MSG_DBGMSG)
		{
			// dbg msg
			$str = buf_get_str($tcp_in_buf);
			dbg_log("dbgmsg: ".$str);
		}
		else if ($msg_id == MSG_PING)
			send_msg(MSG_PONG);
		else if ($msg_id == MSG_CONNECT)
		{
			$s_id = buf_get_uint16($tcp_in_buf);
			$addr_type = buf_get_uint8($tcp_in_buf);
			$addr_data = "";

			if ($addr_type == ADDR_IPV4)
			{
				$addr_data = buf_get_uint8($tcp_in_buf).".".
								buf_get_uint8($tcp_in_buf).".".
								buf_get_uint8($tcp_in_buf).".".
								buf_get_uint8($tcp_in_buf);
			}
			else //TODO:
				return true;

			$port = buf_get_uint16($tcp_in_buf);

			dbg_log("connect to ".$addr_data.":".$port);
			
			$sess_sock = socket_create(AF_INET, SOCK_STREAM, 0);
			socket_set_nonblock($sess_sock);
	
			// connect
			@socket_connect($sess_sock, $addr_data, $port);
			
			// initiate tcp session
			$sessions[$s_id] = array("state" => SESSION_CONNECTING,
									"addr_type" => $addr_type,
									"addr_data" => $addr_data,
									"sock" => $sess_sock,
									"in_buf" => "",
									"out_buf" => "");
		}
		else
			buf_skip($tcp_in_buf, $msg_size);
		
		return true;
	}

	function send_data()
	{
		global $tcp_out_buf, $srv_sock;

		if (mb_strlen($tcp_out_buf) == 0)
			return true;

		$res = socket_send($srv_sock, $tcp_out_buf, mb_strlen($tcp_out_buf), 0);
		if ($res == false || $res <= 0)
		{
			dbg_log("socket_send() returned ".$res);
			return false;
		}
		
		dbg_log($res." bytes sent");
		buf_skip($tcp_out_buf, $res);

		return true;
	}

	// queue a message
	function send_msg($id, $data="")
	{
		global $tcp_out_buf;
		
		$size = mb_strlen($data);

		// size
		buf_put_int16($tcp_out_buf, $size);
		// msg id
		buf_put_int8($tcp_out_buf, $id);
		// data
		buf_put_data($tcp_out_buf, $data);

		dbg_log("send msg ".$id);
	}

	function send_dbg($dbgmsg)
	{
		$data = "";
		buf_put_str($data, $dbgmsg);
		send_msg(MSG_DBGMSG, $data);
	}

	// msg ids
	const MSG_DBGMSG=0;
	const MSG_PING=1;
	const MSG_PONG=2;
	const MSG_CONNECT=3;
	const MSG_CONN_STATE=4;

	// address types
	const ADDR_IPV4=0;
	const ADDR_IPV6=1;
	const ADDR_DOMAIN=2;

	// session states
	const SESSION_CONNECTING=0;
	const SESSION_ONLINE=1;
	const SESSION_FAILED=2;

	// connection states
	const CONN_STATE_ONLINE=0;
	const CONN_STATE_ERROR=1;

	$tcp_in_buf = "";
	$tcp_out_buf = "";
	$null = NULL;
	$dst_ip = $_GET['ip'];
	$dst_port = (int)$_GET['port'];
	$sessions = array();

	$srv_sock = socket_create(AF_INET, SOCK_STREAM, 0);
	
	socket_set_block($srv_sock);

	dbg_log("connecting...");

	@$res = socket_connect($srv_sock, $dst_ip, $dst_port);

	if ($res)
		dbg_log("connected ".$res);
	else
	{
		dbg_log("failed to connect");
		exit();
	}

	send_dbg("hallo");

	while(1)
	{
		$rsocks = array($srv_sock);
		$wsocks = array();

		if (mb_strlen($tcp_out_buf) > 0)
			// we need to send data
			$wsocks[] = $srv_sock;

		// check for descriptors in tcp sessions
		foreach ($sessions as $s_id => &$s)
		{			
			if ($s['state'] == SESSION_CONNECTING)
			{
				$wsocks[] = $s['sock'];
				dbg_log("session ".$s_id." connects to ".$s['addr_data']);
			}
		}

		if (!socket_select($rsocks, $wsocks, $null, $null))
		{
			dbg_log("select error");
			break;
		}

		if (!send_data())
			break;

		$buf;
		if (in_array($srv_sock, $rsocks))
		{
			dbg_log("recv isset");
			$res = socket_recv($srv_sock, $buf, 2048, 0);

			if ($res > 0)
			{
				// add bytes to tcp buffer
				buf_put_data($tcp_in_buf, $buf);
			
				while (handle_data());
			}
			else
			{
				dbg_log("recv returned ".$res);
				break;
			}
		}

		// handle sessions
		foreach ($sessions as $s_id => &$s)
		{			
			if ($s['state'] == SESSION_CONNECTING &&
				in_array($s['sock'], $wsocks))
			{
				@$res = socket_connect($s['sock'], $s['addr_data'], $s['port']);
				if ($res)
				{
					dbg_log("session ".$s_id." established");
					$s['state'] = SESSION_ONLINE;

					// inform rev
					$msg = "";
					buf_put_int16($msg, $s_id);
					buf_put_int8($msg, CONN_STATE_ONLINE);
					send_msg(MSG_CONN_STATE, $msg);
				}
				else
				{
					dbg_log("session ".$s_id." not established: ".socket_strerror(socket_last_error()));
					$s['state'] = SESSION_FAILED;
					
					// inform rev
					$msg = "";
					buf_put_int16($msg, $s_id);
					buf_put_int8($msg, CONN_STATE_ERROR);
					buf_put_str($msg, socket_strerror(socket_last_error()));
					send_msg(MSG_CONN_STATE, $msg);

					// unset session in array
					unset($s);
				}
			}
		}
	}
?>
