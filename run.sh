#!/bin/bash
gdb --quiet -ex run --args ./rev_server 127.0.0.1 3443 http://127.0.0.1:8080/sockssrv.php 60 127.0.0.1 1080
