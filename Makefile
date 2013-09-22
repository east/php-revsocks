CC = gcc
CFLAGS = -Wall -Wno-unused-function -g
LIBS = 
OBJS = regex_url.o fifobuf.o system.o rev_server.o rev_network.o main.o

all: rev_server

clean:
	rm -Rf rev_server *.o

rev_server: $(OBJS)
	$(CC) -o rev_server $(OBJS) $(CFLAGS) $(LIBS)

system.o: system.h
regex_url.o: regex_url.h
fifobuf.o: fifobuf.h
rev_server.o: rev_server.h rev_network.h system.h regex_url.h
rev_network.o: rev_network.h
