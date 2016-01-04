systemdpreset_DATA = \
	50-zfs.preset

systemdunit_DATA = \
	zfs-zed.service \
	zfs-import-cache.service \
	zfs-import-scan.service \
	zfs-mount.service \
	zfs-share.service \
	zfs.target

EXTRA_DIST = \
	$(top_srcdir)/etc/systemd/system/zfs-zed.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-import-cache.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-import-scan.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-mount.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-share.service.in \
	$(top_srcdir)/etc/systemd/system/zfs.target.in \
	$(top_srcdir)/etc/systemd/system/50-zfs.preset.in

$(systemdunit_DATA):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		"$(top_srcdir)/etc/systemd/system/$@.in" >'$@'

$(systemdpreset_DATA):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		"$(top_srcdir)/etc/systemd/system/$@.in" >'$@'

distclean-local::
	-$(RM) $(systemdunit_DATA) $(systemdpreset_DATA)
