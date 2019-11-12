dist_man_MANS = \
	fsck.zfs.8 \
	mount.zfs.8 \
	vdev_id.8 \
	zdb.8 \
	zfs.8 \
	zfsconcepts.8 \
	zfsprops.8 \
	zfs-allow.8 \
	zfs-bookmark.8 \
	zfs-change-key.8 \
	zfs-clone.8 \
	zfs-create.8 \
	zfs-destroy.8 \
	zfs-diff.8 \
	zfs-get.8 \
	zfs-groupspace.8 \
	zfs-hold.8 \
	zfs-inherit.8 \
	zfs-list.8 \
	zfs-load-key.8 \
	zfs-mount.8 \
	zfs-program.8 \
	zfs-project.8 \
	zfs-projectspace.8 \
	zfs-promote.8 \
	zfs-receive.8 \
	zfs-recv.8 \
	zfs-redact.8 \
	zfs-release.8 \
	zfs-rename.8 \
	zfs-rollback.8 \
	zfs-send.8 \
	zfs-set.8 \
	zfs-share.8 \
	zfs-snapshot.8 \
	zfs-unallow.8 \
	zfs-unload-key.8 \
	zfs-unmount.8 \
	zfs-upgrade.8 \
	zfs-userspace.8 \
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
