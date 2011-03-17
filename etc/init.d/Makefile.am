EXTRA_DIST = zfs.fedora zfs.lsb

install-data-local:
	@instdest=$(DESTDIR)/$(sysconfdir)/init.d/zfs; \
	$(INSTALL) -TD zfs.$(DEFAULT_INIT_SCRIPT) $$instdest
