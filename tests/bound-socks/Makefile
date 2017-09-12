
include ../../Makefile.env

CFLAGS+=        -Wall -g -O2 -I../../include
LIBS+=		../../src/obj/libpvd.a


all :	pvd-bound-socks pvd-test-saddr

pvd-bound-socks : pvd-bound-socks.o
	$(CC) -g -o pvd-bound-socks pvd-bound-socks.o $(LIBS)

pvd-test-saddr : pvd-test-saddr.o
	$(CC) -g -o pvd-test-saddr pvd-test-saddr.o $(LIBS)

clean :
	/bin/rm -f pvd-bound-socks pvd-bound-socks.o
	/bin/rm -f pvd-test-saddr pvd-test-saddr.o
