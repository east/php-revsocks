CC = gcc
CFLAGS = -Wall -Wno-unused-function -g
LIBS = 
OBJS = regex_url.o fifobuf.o system.o rev_server.o

all: rev_server

clean:
	rm -Rf rev_server *.o

rev_server: $(OBJS)
	$(CC) -o rev_server $(OBJS) $(CFLAGS) $(LIBS)
