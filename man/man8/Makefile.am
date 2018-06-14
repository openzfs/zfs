dist_man_MANS = \
	fsck.zfs.8 \
	mount.zfs.8 \
	vdev_id.8 \
	zdb.8 \
	zfs.8 \
	zfs-program.8 \
	zgenhostid.8 \
	zinject.8 \
	zpool.8 \
	zstreamdump.8

nodist_man_MANS = \
	zed.8 \
	zfs-mount-generator.8

EXTRA_DIST = \
	zed.8.in \
	zfs-mount-generator.8.in

$(nodist_man_MANS): %: %.in
	-$(SED) -e 's,@zfsexecdir\@,$(zfsexecdir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

install-data-local:
	$(INSTALL) -d -m 0755 "$(DESTDIR)$(mandir)/man8"

CLEANFILES = \
	$(nodist_man_MANS)
