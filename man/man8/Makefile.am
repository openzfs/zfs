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
	zpoolconcepts.8 \
	zpoolprops.8 \
	zpool-add.8 \
	zpool-attach.8 \
	zpool-checkpoint.8 \
	zpool-clear.8 \
	zpool-create.8 \
	zpool-destroy.8 \
	zpool-detach.8 \
	zpool-events.8 \
	zpool-export.8 \
	zpool-get.8 \
	zpool-history.8 \
	zpool-import.8 \
	zpool-initialize.8 \
	zpool-iostat.8 \
	zpool-labelclear.8 \
	zpool-list.8 \
	zpool-offline.8 \
	zpool-online.8 \
	zpool-reguid.8 \
	zpool-remove.8 \
	zpool-reopen.8 \
	zpool-replace.8 \
	zpool-resilver.8 \
	zpool-scrub.8 \
	zpool-set.8 \
	zpool-split.8 \
	zpool-status.8 \
	zpool-sync.8 \
	zpool-trim.8 \
	zpool-upgrade.8 \
	zpool-wait.8 \
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
