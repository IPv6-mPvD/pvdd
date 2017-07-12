
include ../Makefile.env

$(MAKECMDGOALS) :

install :
	/bin/mkdir -p $(DESTDIR)/include &&\
	/bin/cp libpvd.h $(DESTDIR)/include

uninstall cleaninstall :
	/bin/rm -f \
		$(DESTDIR)/include/libpvd.h
