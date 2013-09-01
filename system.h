#ifndef SYSTEM_H
#define SYSTEM_H

#define ASSERT(test, info) if (!(test)) { *((char*)NULL) = 0; }

int create_tcp_socket();
void socket_set_block(int sock, int block);


#endif
