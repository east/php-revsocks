<?php
	function buf_len(&$buf) { return mb_strlen($buf); }
	function buf_put_data(&$buf, $data) { $buf .= $data; }
	function buf_put_str(&$buf, $str)
	{
		$len = mb_strlen($str);

		for ($i = 0; $i < $len; $i++)
			$buf .= pack("c", ord(substr($str, $i, 1)));

		$buf .= pack("c", 0);
	}
	function buf_str_len(&$buf)
	{
		$len = mb_strlen($buf);
		$str_len = 0;

		for ($i = 0; $i < $len; $i++) {
			$char_ord = unpack("c", substr($buf, $i, 1));
			$char_ord = $char_ord[1];
			if ($char_ord == 0)
				break; // cstring end
			$str_len++;
		}

		return $str_len;
	}
	function buf_put_int8(&$buf, $num) { $buf .= pack("c", $num); }	
	function buf_put_int16(&$buf, $num) { $buf .= pack("s", $num); }	
	function buf_get_str(&$buf)
	{
		$len = buf_str_len($buf);
		$str_array = unpack("c".$len, $buf);

		for ($i = 1; $i <= $len; $i++)
			$str .= chr($str_array[$i]);

		$buf = substr($buf, $len+1);

		return $str;
	}
	function buf_get_data(&$buf, $size)
	{
		if (buf_len($buf) < $size)
			$size = buf_len($buf);

		if ($size == 0)
			return "";

		$data = substr($buf, 0, $size);
		buf_skip($buf, $size);
		return $data;
	}
	function buf_skip(&$buf, $len) { $buf = substr($buf, $len); }
	function buf_get_uint8(&$buf, $do_skip=true)
	{
		$num = unpack("C", $buf);
		$num = $num[1];

		if ($do_skip)
			buf_skip($buf, 1);
		return $num;
	}
	function buf_get_uint16(&$buf, $do_skip=true)
	{
		$num = unpack("S", $buf);
		$num = $num[1];

		if ($do_skip)
			buf_skip($buf, 2);
		return $num;
	}
?>
