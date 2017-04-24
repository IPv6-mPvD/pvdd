
SFDAEMON=	pvdid-daemon.c pvdid-netlink.c pvdid-utils.c
OFDAEMON=	$(SFDAEMON:.c=.o)

SFLIB=		libpvdid.c
OFLIB=		$(SFLIB:.c=.o)

CFLAGS+=	-Wall -O0 -g -I../include

all :	pvdid-daemon libpvdid.so libpvdid.a

pvdid-daemon :	$(OFDAEMON)
	$(CC) -g -o $@ $(OFDAEMON)

$(OFLIB) :	CFLAGS+=-fpic

libpvdid.so :	$(OFLIB)
	$(CC) -shared -o $@ $(OFLIB)

libpvdid.a :	$(OFLIB)
	ar rcv $@ $(OFLIB)

clean :
	/bin/rm -f pvdid-daemon libpvdid.so libpvdid.a *.o
