EXTRA_DIST = zfs.fedora zfs.gentoo zfs.lsb zfs.lunar zfs.redhat

install-data-local:
	@instdest=$(DESTDIR)/$(sysconfdir)/init.d/zfs; \
	$(INSTALL) -TD zfs.$(DEFAULT_INIT_SCRIPT) $$instdest
