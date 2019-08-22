systemdpreset_DATA = \
	50-zfs.preset

systemdunit_DATA = \
	zfs-zed.service \
	zfs-import-cache.service \
	zfs-import-scan.service \
	zfs-mount.service \
	zfs-share.service \
	zfs-volume-wait.service \
	zfs-import.target \
	zfs-volumes.target \
	zfs.target

EXTRA_DIST = \
	$(top_srcdir)/etc/systemd/system/zfs-zed.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-import-cache.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-import-scan.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-mount.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-share.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-import.target.in \
	$(top_srcdir)/etc/systemd/system/zfs-volume-wait.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-volumes.target.in \
	$(top_srcdir)/etc/systemd/system/zfs.target.in \
	$(top_srcdir)/etc/systemd/system/50-zfs.preset.in

$(systemdunit_DATA) $(systemdpreset_DATA):%:%.in
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(systemdunitdir)"
	ln -sf /dev/null "$(DESTDIR)$(systemdunitdir)/zfs-import.service"

distclean-local::
	-$(RM) $(systemdunit_DATA) $(systemdpreset_DATA)
