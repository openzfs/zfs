systemdpreset_DATA = \
	$(top_srcdir)/etc/systemd/system/50-zfs.preset
systemd_DATA = \
	$(top_srcdir)/etc/systemd/system/zfs-module.service \
	$(top_srcdir)/etc/systemd/system/zfs-import.service \
	$(top_srcdir)/etc/systemd/system/zfs-mount.service \
	$(top_srcdir)/etc/systemd/system/zfs-share.service \
	$(top_srcdir)/etc/systemd/system/zfs.target

EXTRA_DIST = \
	$(top_srcdir)/etc/systemd/system/zfs-module.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-import.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-mount.service.in \
	$(top_srcdir)/etc/systemd/system/zfs-share.service.in \
	$(top_srcdir)/etc/systemd/system/zfs.target.in \
	$(top_srcdir)/etc/systemd/system/50-zfs.preset.in

$(systemd_DATA):
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
	-$(RM) $(systemd_DATA) $(systemdpreset_DATA)
