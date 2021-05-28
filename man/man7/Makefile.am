dist_man_MANS = \
        spl-module-parameters.7 \
        zfsconcepts.7 \
        zfsprops.7 \
        zfs-events.7 \
        zfs-module-parameters.7 \
        zpoolconcepts.7 \
        zpoolprops.7 \
        zpool-features.7

if BUILD_LINUX
# The man pager in most Linux distros defaults to BSD instead of Linux
# when .Os is blank, but leaving it blank makes things a lot easier on
# FreeBSD when OpenZFS is vendored in the base system.
install-data-hook:
	cd $(DESTDIR)$(mandir)/man7; \
	$(SED) ${ac_inplace} -e 's/^\.Os$$/.Os Linux/' \
	    $(dist_man_MANS)
endif
