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
		global $tcp_buf;

		$len = mb_strlen($tcp_buf);

		if ($len < 3)
			return false;

		$msg_size = buf_get_uint16($tcp_buf, false);
		$msg_id = buf_get_uint8(substr($tcp_buf, 2), false);
		dbg_log("msg ".$msg_id." len ".$msg_size);

		if (mb_strlen($tcp_buf) < 3 + $msg_size)
			return false; // not enough data

		buf_skip($tcp_buf, 3);

		if ($msg_id == MSG_DBGMSG)
		{
			// dbg msg
			$str = buf_get_str($tcp_buf);
			dbg_log("dbgmsg: ".$str);
		}
		else
			buf_skip($tcp_buf, $msg_size);
		
		return true;
	}

	// msg ids
	const MSG_DBGMSG=0;

	$tcp_buf = "";
	$null = NULL;
	$dst_ip = $_GET['ip'];
	$dst_port = (int)$_GET['port'];

	$srv_sock = socket_create(AF_INET, SOCK_STREAM, 0);//fsockopen($dst_ip, $dst_port);

	socket_connect($srv_sock, $dst_ip, $dst_port);

	dbg_log("connected");

	while(1)
	{
		$rsocks = array($srv_sock);
		
		if (!socket_select($rsocks, $null, $null, $null))
		{
			dbg_log("select error");
			break;
		}

		$buf;
		$res = socket_recv($srv_sock, $buf, 2048, 0);

		if ($res > 0)
		{
			// add bytes to tcp buffer
			buf_put_data($tcp_buf, $buf);
			
			while (handle_data());
		}
		else
		{
			dbg_log("recv returned ".$res);
			break;
		}
	}
?>
