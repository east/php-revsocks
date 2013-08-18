CC = gcc
CFLAGS = -Wall -Wno-unused-function -g
LIBS = 
OBJS = main.o regex_url.o fifobuf.o

all: test

clean:
	rm -Rf test *.o

test: $(OBJS)
	$(CC) -o test $(OBJS) $(CFLAGS) $(LIBS)
