
include ../Makefile.env

$(MAKECMDGOALS) ::

all ::

install ::
	/bin/mkdir -p $(DESTDIR)/libexec &&\
	/bin/cp pvd-monitor.js $(DESTDIR)/libexec
	/bin/mkdir -p $(DESTDIR)/sbin &&\
	sed -e "s?@PREFIX?$(DESTDIR)?" <pvd-monitor.tmpl >$(DESTDIR)/sbin/pvd-monitor &&\
	/bin/chmod 755 $(DESTDIR)/sbin/pvd-monitor

uninstall cleaninstall ::
	/bin/rm -f \
		$(DESTDIR)/libexec/pvd-monitor.js \
		$(DESTDIR)/sbin/pvd-monitor
