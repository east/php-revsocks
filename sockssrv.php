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
		{
			dbg_log("got ping, send pong");
			send_msg(MSG_PONG);
		}
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
		else if ($msg_id == MSG_SEND)
		{
			$s_id = buf_get_uint16($tcp_in_buf);
			$size = buf_get_uint16($tcp_in_buf);
			$data = buf_get_data($tcp_in_buf, $size);
	
			if ($size == 0)
			{
				dbg_log("revsrv wants to disconnect session ".$s_id);
			
				socket_close($s['sock']);
				// unset session in array
				unset($s);
			}
			else
			{
				if ($size > buf_space_left($sessions[$s_id]['out_buf']))
				{
					dbg_log("session out buffer overflow");
					exit();
				}

				buf_put_data($sessions[$s_id]['out_buf'], $data);
			}
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
		
		//dbg_log($res." bytes sent");
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
	}

	function send_dbg($dbgmsg)
	{
		$data = "";
		buf_put_str($data, $dbgmsg);
		send_msg(MSG_DBGMSG, $data);
	}

	function buf_space_left(&$buf)
	{
		return NETWORK_BUF_SIZE - buf_len($buf);
	}

	const NETWORK_BUF_SIZE=16384; //4*4096
	const MAX_MSG_SIZE=4096;
	
	// msg ids
	const MSG_DBGMSG=0;
	const MSG_PING=1;
	const MSG_PONG=2;
	const MSG_CONNECT=3;
	const MSG_CONN_STATE=4;
	const MSG_SEND=5;

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
	const CONN_STATE_CONNECTING=2;
	const CONN_STATE_OFFLINE=3;

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
	send_msg(MSG_PING);

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
			else if($s['state'] == SESSION_ONLINE &&
					buf_space_left($s['in_buf']) > 0)
				$rsocks[] = $s['sock'];
		}

		if (!socket_select($rsocks, $wsocks, $null, 1))
		{
			//dbg_log("select error");
			//break;
		}

		if (!send_data())
			break;

		$buf;
		if (in_array($srv_sock, $rsocks))
		{
			$res = socket_recv($srv_sock, $buf, 2048, 0);

			if ($res && $res > 0)
			{
				// add bytes to tcp buffer
				buf_put_data($tcp_in_buf, $buf);
			
				while (handle_data());
			}
			else
			{
				dbg_log("recv returned '".$res."' ".socket_str_error(socket_last_error()));
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
					buf_put_str($msg, "");
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
			else if ($s['state'] == SESSION_ONLINE)
			{
				if (in_array($s['sock'], $rsocks))
				{
					$buf;

					$space_left = buf_space_left($s['in_buf']);
					$res = socket_recv($s['sock'], $buf, $space_left, 0);

					if ($res == false || $res <= 0)
					{
						// disconnect?
						dbg_log("session ".$s_id." disc");

						$s['state'] = SESSION_FAILED;
						socket_close($s['sock']);

						// inform rev
						$msg = "";
						buf_put_int16($msg, $s_id);
						buf_put_int8($msg, CONN_STATE_ERROR);
						buf_put_str($msg, socket_strerror(socket_last_error()));
						send_msg(MSG_CONN_STATE, $msg);

						// unset session in array
						unset($s);
					}
					else
					{
						//dbg_log("session ".$s_id." got ".$res." bytes");				
						buf_put_data($s['in_buf'], $buf);
					}
				}

				// send avaiable data to rev server
				$len = buf_len($s['in_buf']);
				if ($len > 0)
				{
					if ($len > MAX_MSG_SIZE-4)
						$len = MAX_MSG_SIZE-4;
					$msg = "";
					buf_put_int16($msg, $s_id);
					buf_put_int16($msg, $len);
					$buf_data = buf_get_data($s['in_buf'], $len);
					buf_put_data($msg, $buf_data);
					send_msg(MSG_SEND, $msg);
				}

				// send avaiable data to our destination host
				$len = buf_len($s['out_buf']);
				if ($len > 0)
				{
					$res = socket_send($s['sock'], $s['out_buf'], $len, 0);

					if ($res > 0)
						buf_skip($s['out_buf'], $res);
				}
			}
		}
	}
?>
