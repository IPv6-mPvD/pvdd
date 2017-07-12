
include ../../Makefile.env

JSFILES=	pvdd.js

all ::

$(MAKECMDGOALS) ::

install ::
	@/bin/mkdir -p $(DESTDIR)/lib/node_modules
	@echo Ignoring : /bin/cp $(JSFILES) $(DESTDIR)/lib/node_modules
	/bin/rm -f pvdd-*.tgz
	npm pack pvdd && npm -g install ./pvdd-*.tgz

uninstall cleaninstall ::
	@for f in $(JSFILES); do\
		echo Ignoring /bin/rm -f \
			$(DESTDIR)/lib/node_modules/"$$f";\
	done
	npm -g uninstall pvdd
