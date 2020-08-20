dist_man_MANS = \
	vdev_id.conf.5 \
	zpool-features.5 \
	spl-module-parameters.5 \
	zfs-module-parameters.5 \
	zfs-events.5

if BUILD_LINUX
# The man pager in most Linux distros defaults to BSD instead of Linux
# when .Os is blank, but leaving it blank makes things a lot easier on
# FreeBSD when OpenZFS is vendored in the base system.
install-data-hook:
	cd $(DESTDIR)$(mandir)/man5; \
	$(SED) ${ac_inplace} -e 's/^\.Os$$/.Os Linux/' \
	    $(dist_man_MANS)
endif
