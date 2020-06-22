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
	zfs-zed.service.in \
	zfs-import-cache.service.in \
	zfs-import-scan.service.in \
	zfs-mount.service.in \
	zfs-share.service.in \
	zfs-import.target.in \
	zfs-volume-wait.service.in \
	zfs-volumes.target.in \
	zfs.target.in \
	50-zfs.preset.in

$(systemdunit_DATA) $(systemdpreset_DATA):%:%.in
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(systemdunitdir)"
	ln -sf /dev/null "$(DESTDIR)$(systemdunitdir)/zfs-import.service"

# Double-colon rules are allowed; there are multiple independent definitions.
distclean-local::
	-$(RM) $(systemdunit_DATA) $(systemdpreset_DATA)
