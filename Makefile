CC = gcc
CFLAGS = -Wall -Wno-unused-function -g
LIBS = 

all: test

clean:
	rm -Rf test *.o

test: main.o regex_url.o 
	$(CC) -o test main.o regex_url.o $(CFLAGS) $(LIBS)
