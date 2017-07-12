
include ../../Makefile.env

CFLAGS+=        -Wall -g -O2 -I../../include
LIBS+=		../../src/obj/libpvd.a


pvd-bound-socks : pvd-bound-socks.o
	$(CC) -g -o pvd-bound-socks pvd-bound-socks.o $(LIBS)

clean :
	/bin/rm -f pvd-bound-socks pvd-bound-socks.o
