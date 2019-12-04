initrddir = /usr/share/initramfs-tools

initrd_SCRIPTS = \
	conf.d/zfs conf-hooks.d/zfs hooks/zfs scripts/zfs scripts/local-top/zfs

SUBDIRS = hooks scripts

EXTRA_DIST = \
	$(top_srcdir)/etc/init.d/zfs \
	$(top_srcdir)/etc/init.d/zfs-functions \
	$(top_srcdir)/contrib/initramfs/conf.d/zfs \
	$(top_srcdir)/contrib/initramfs/conf-hooks.d/zfs \
	$(top_srcdir)/contrib/initramfs/README.initramfs.markdown

$(top_srcdir)/etc/init.d/zfs $(top_srcdir)/etc/init.d/zfs-functions:
	$(MAKE) -C $(top_srcdir)/etc/init.d zfs zfs-functions

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
	$(MKDIR_P) $(DESTDIR)$(DEFAULT_INITCONF_DIR); \
	cp $(top_builddir)/etc/init.d/zfs \
	    $(DESTDIR)$(DEFAULT_INITCONF_DIR)/; \
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/zfs; \
	cp $(top_builddir)/etc/init.d/zfs-functions \
	    $(DESTDIR)$(sysconfdir)/zfs/
