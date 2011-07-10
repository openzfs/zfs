EXTRA_DIST = zfs.fedora zfs.gentoo zfs.lsb zfs.lunar zfs.redhat zfs.arch

install-data-local:
	@instdest=$(DESTDIR)$(DEFAULT_INIT_DIR)/zfs; \
	$(INSTALL) -TD zfs.$(DEFAULT_INIT_SCRIPT) $$instdest
