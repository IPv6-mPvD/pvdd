
include ./Makefile.env

ifeq (0,$(MAKELEVEL))
all $(MAKECMDGOALS) :
	@-$(MAKE) -C include $@
	@-$(MAKE) -C src $@
	@-$(MAKE) -C utils $@
	@-$(MAKE) -C bindings $@
endif
