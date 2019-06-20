initrddir = $(datarootdir)/initramfs-tools

initrd_SCRIPTS = \
	conf.d/zfs conf-hooks.d/zfs hooks/zfs scripts/zfs scripts/local-top/zfs

SUBDIRS = hooks scripts

EXTRA_DIST = \
	$(top_srcdir)/contrib/initramfs/conf.d/zfs \
	$(top_srcdir)/contrib/initramfs/conf-hooks.d/zfs \
	$(top_srcdir)/contrib/initramfs/README.initramfs.markdown

install-initrdSCRIPTS: $(EXTRA_DIST)
	for d in conf.d conf-hooks.d scripts/local-top; do \
		$(MKDIR_P) $(DESTDIR)$(initrddir)/$$d; \
		cp $(top_srcdir)/contrib/initramfs/$$d/zfs \
		    $(DESTDIR)$(initrddir)/$$d/; \
	done
	for d in hooks scripts; do \
		$(MKDIR_P) $(DESTDIR)$(initrddir)/$$d; \
		cp $(top_builddir)/contrib/initramfs/$$d/zfs \
		    $(DESTDIR)$(initrddir)/$$d/; \
	done
	if [ -f $(top_builddir)/etc/init.d/zfs ]; then \
		$(MKDIR_P) $(DESTDIR)$(DEFAULT_INITCONF_DIR); \
		cp $(top_builddir)/etc/init.d/zfs \
		    $(DESTDIR)$(DEFAULT_INITCONF_DIR)/; \
	fi
