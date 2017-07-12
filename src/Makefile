
include ../Makefile.env

SFDAEMON=	pvdd.c pvdd-netlink.c pvdd-rtnetlink.c pvd-utils.c
OFDAEMON=	$(SFDAEMON:%.c=obj/%.o)

SFLIB=		libpvd.c libpvd-utils.c
OFLIB=		$(SFLIB:%.c=obj/%.o)

CFLAGS+=	-Wall -g -O2 -I../include

all :	obj obj/pvdd obj/libpvd.so obj/libpvd.a obj/libpvd-test

AR=	ar
MKDIR=	mkdir
RM=	/bin/rm
CP=	/bin/cp

SRCS=	\
	libpvd.c		\
	libpvd-test.c		\
	libpvd-utils.c		\
	pvdd.c			\
	pvdd-netlink.c		\
	pvdd-rtnetlink.c	\
	pvd-utils.c

obj :
	$(MKDIR) -p obj

obj/%.o:%.c
	$(CC) -c $(CFLAGS) -o $@ $<

obj/pvdd-netlink.o:	CFLAGS+=-fno-strict-aliasing

obj/pvdd : $(OFDAEMON) obj/libpvd.a
	$(CC) -g -o $@ $^

$(OFLIB) :	CFLAGS+=-fpic -O2

obj/libpvd.so : $(OFLIB)
	$(CC) -shared -o $@ $^

obj/libpvd.a : $(OFLIB)
	$(AR) rcv $@ $^

obj/libpvd-test : obj/libpvd-test.o obj/libpvd.a
	$(CC) -g -o $@ $^

clean :
	$(RM) -rf obj pvdd libpvd-test libpvd.so libpvd.a *.o

install : all
	$(MKDIR) -p $(DESTDIR)/include $(DESTDIR)/sbin $(DESTDIR)/libexec $(DESTDIR)/lib &&\
	$(CP) obj/pvdd $(DESTDIR)/sbin &&\
	$(CP) obj/libpvd.so obj/libpvd.a $(DESTDIR)/lib

uninstall cleaninstall :
	$(RM) -f \
		$(DESTDIR)/sbin/pvdd \
		$(DESTDIR)/lib/libpvd.so \
		$(DESTDIR)/lib/libpvd.a

cleandepend :
	$(RM) -f ./obj/Makefile.depend

depend : obj
	for f in $(SRCS); do\
		$(CC) $(CFLAGS) -M -MT obj/"`basename $$f .c`.o" $$f -o -;\
	done >./obj/Makefile.depend

-include ./obj/Makefile.depend

