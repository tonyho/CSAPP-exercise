CC = gcc
CFLAGS = -O2 -Wall -I .

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread
LIB += -lrt


all: tiny cgi

tiny: tiny.c csapp.o sbuf.o
	$(CC) $(CFLAGS) -o tiny_multi_thread   tiny.c csapp.o sbuf.o $(LIB) 
csapp.o:
	$(CC) $(CFLAGS) -c csapp.c
sbuf.o :
	$(CC) -c $(CFLAGS) sbuf.c 
cgi:
	(cd cgi-bin; make)

clean:
	rm -f *.o tiny *~
	(cd cgi-bin; make clean)

