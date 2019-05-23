initrddir = $(datarootdir)/initramfs-tools

initrd_SCRIPTS = \
	conf.d/zfs conf-hooks.d/zfs hooks/zfs scripts/zfs scripts/local-top/zfs

SUBDIRS = hooks scripts ../../etc

EXTRA_DIST = \
	$(top_srcdir)/etc/init.d/zfs \
	$(top_srcdir)/etc/init.d/zfs-functions \
	$(top_srcdir)/contrib/initramfs/conf.d/zfs \
	$(top_srcdir)/contrib/initramfs/conf-hooks.d/zfs \
	$(top_srcdir)/contrib/initramfs/README.initramfs.markdown

install-initrdSCRIPTS: $(EXTRA_DIST)
	for d in conf.d conf-hooks.d hooks scripts scripts/local-top; do \
	  $(MKDIR_P) -v $(DESTDIR)$(initrddir)/$$d; \
	  cp -v $(top_srcdir)/contrib/initramfs/$$d/zfs \
	    $(DESTDIR)$(initrddir)/$$d/; \
	done

	$(MKDIR_P) -v $(DESTDIR)$(DEFAULT_INITCONF_DIR) $(DESTDIR)/etc/zfs/

	cp -v $(top_srcdir)/etc/init.d/zfs \
	  $(DESTDIR)$(DEFAULT_INITCONF_DIR)/

	$(MKDIR_P) -v $(DESTDIR)/etc/init.d/

	cp -v $(top_srcdir)/etc/init.d/zfs-functions \
	  $(DESTDIR)/etc/init.d/
