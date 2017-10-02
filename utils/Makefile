
include ../Makefile.env

$(MAKECMDGOALS) ::

CFLAGS+=        -Wall -g -O2 -I../include
LIBS+=		../src/obj/libpvd.a


all ::	pvddo

pvddo : pvddo.o
	$(CC) -o pvddo pvddo.o $(LIBS)

clean ::
	/bin/rm -f pvddo pvddo.o
	
install :: pvddo
	/bin/mkdir -p $(DESTDIR)/libexec &&\
	/bin/cp pvd-monitor.js $(DESTDIR)/libexec
	/bin/mkdir -p $(DESTDIR)/sbin &&\
	sed -e "s?@PREFIX?$(DESTDIR)?" <pvd-monitor.tmpl >$(DESTDIR)/sbin/pvd-monitor &&\
	/bin/chmod 755 $(DESTDIR)/sbin/pvd-monitor
	/bin/cp pvddo $(DESTDIR)/sbin
	/bin/chmod 755 $(DESTDIR)/sbin/pvddo
	/usr/bin/strip $(DESTDIR)/sbin/pvddo

uninstall cleaninstall ::
	/bin/rm -f \
		$(DESTDIR)/libexec/pvd-monitor.js \
		$(DESTDIR)/sbin/pvd-monitor \
		$(DESTDIR)/sbin/pvddo
