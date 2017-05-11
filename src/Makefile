

SFDAEMON=	pvdid-daemon.c pvdid-netlink.c pvdid-utils.c
OFDAEMON=	$(SFDAEMON:%.c=obj/%.o)

SFLIB=		libpvdid.c libpvdid-utils.c
OFLIB=		$(SFLIB:%.c=obj/%.o)

CFLAGS+=	-Wall -g -O2 -I../include

all :	obj obj/pvdid-daemon obj/libpvdid.so obj/libpvdid.a obj/libpvdid-test

AR=	ar
MKDIR=	mkdir
RM=	/bin/rm
CP=	/bin/cp

SRCS=	\
	libpvdid.c		\
	libpvdid-test.c		\
	libpvdid-utils.c	\
	pvdid-daemon.c		\
	pvdid-netlink.c		\
	pvdid-utils.c

obj :
	$(MKDIR) -p obj

obj/%.o:%.c
	$(CC) -c $(CFLAGS) -o $@ $<

obj/pvdid-netlink.o:	CFLAGS+=-fno-strict-aliasing

obj/pvdid-daemon : $(OFDAEMON) obj/libpvdid.a
	$(CC) -g -o $@ $^

$(OFLIB) :	CFLAGS+=-fpic -O2

obj/libpvdid.so : $(OFLIB)
	$(CC) -shared -o $@ $^

obj/libpvdid.a : $(OFLIB)
	$(AR) rcv $@ $^

obj/libpvdid-test : obj/libpvdid-test.o obj/libpvdid.a
	$(CC) -g -o $@ $^

clean :
	$(RM) -rf obj pvdid-daemon libpvdid-test libpvdid.so libpvdid.a *.o

install : all
	$(MKDIR) -p $(DESTDIR)/include $(DESTDIR)/sbin $(DESTDIR)/libexec $(DESTDIR)/lib &&\
	$(CP) obj/pvdid-daemon $(DESTDIR)/sbin &&\
	$(CP) obj/libpvdid.so obj/libpvdid.a $(DESTDIR)/lib

cleaninstall :
	$(RM) -f \
		$(DESTDIR)/sbin/pvdid-daemon \
		$(DESTDIR)/lib/libpvdid.so \
		$(DESTDIR)/lib/libpvdid.a

cleandepend :
	$(RM) -f ./obj/Makefile.depend

depend : obj
	for f in $(SRCS); do\
		$(CC) $(CFLAGS) -M -MT obj/"`basename $$f .c`.o" $$f -o -;\
	done >./obj/Makefile.depend

-include ./obj/Makefile.depend

