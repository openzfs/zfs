systemdpreset_DATA = \
	$(top_srcdir)/etc/systemd/system/50-zfs.preset
systemdunit_DATA = \
	$(top_srcdir)/etc/systemd/system/zfs-import-scan.service \
	$(top_srcdir)/etc/systemd/system/zfs-import-cache.service \
	$(top_srcdir)/etc/systemd/system/zfs-mount.service \
	$(top_srcdir)/etc/systemd/system/zfs-share.service \
	$(top_srcdir)/etc/systemd/system/zfs.target

EXTRA_DIST = \
	$(top_srcdir)/etc/systemd/system/zfs-import-scan.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-import-cache.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-mount.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-share.service.in \
	$(top_srcdir)/etc/systemd/system/zfs.target.in \
	$(top_srcdir)/etc/systemd/system/50-zfs.preset.in

$(systemdunit_DATA):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		'$@.in' >'$@'

$(systemdpreset_DATA):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		'$@.in' >'$@'

distclean-local::
	-$(RM) $(systemdunit_DATA) $(systemdpreset_DATA)
