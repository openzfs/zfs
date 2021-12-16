include $(top_srcdir)/config/Substfiles.am

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
	zfs.target \
	zfs-scrub-monthly@.timer \
	zfs-scrub-weekly@.timer \
	zfs-scrub@.service

SUBSTFILES += $(systemdpreset_DATA) $(systemdunit_DATA)

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(systemdunitdir)"
	ln -sf /dev/null "$(DESTDIR)$(systemdunitdir)/zfs-import.service"
