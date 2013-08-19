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
		global $tcp_in_buf;

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
		else if($msg_id == MSG_PING)
			send_msg(MSG_PONG);
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

	$tcp_in_buf = "";
	$tcp_out_buf = "";
	$null = NULL;
	$dst_ip = $_GET['ip'];
	$dst_port = (int)$_GET['port'];

	$srv_sock = socket_create(AF_INET, SOCK_STREAM, 0);//fsockopen($dst_ip, $dst_port);

	socket_connect($srv_sock, $dst_ip, $dst_port);

	dbg_log("connected");

	send_dbg("hallo");

	while(1)
	{
		$rsocks = array($srv_sock);
		$wsocks = array();

		if (mb_strlen($tcp_out_buf) > 0)
			// we need to send data
			$wsocks[] = $srv_sock;

		if (!socket_select($rsocks, $wsocks, $null, $null))
		{
			dbg_log("select error");
			break;
		}

		if (!send_data())
			break;

		$buf;
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
?>
