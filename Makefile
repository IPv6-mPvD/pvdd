
all $(MAKECMDGOALS) :
	@-$(MAKE) -C include $@
	@-$(MAKE) -C src $@
	@-$(MAKE) -C utils $@
