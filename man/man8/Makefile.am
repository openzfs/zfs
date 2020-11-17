include $(top_srcdir)/config/Substfiles.am

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
	zfs-jail.8 \
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
	zfs-unjail.8 \
	zfs-unload-key.8 \
	zfs-unmount.8 \
	zfs-upgrade.8 \
	zfs-userspace.8 \
	zfs-wait.8 \
	zfs_ids_to_path.8 \
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
	zstream.8 \
	zstreamdump.8 \
	zpool_influxdb.8

nodist_man_MANS = \
	zed.8 \
	zfs-mount-generator.8

SUBSTFILES += $(nodist_man_MANS)

if BUILD_LINUX
# The man pager in most Linux distros defaults to BSD instead of Linux
# when .Os is blank, but leaving it blank makes things a lot easier on
# FreeBSD when OpenZFS is vendored in the base system.
install-data-hook:
	cd $(DESTDIR)$(mandir)/man8; \
	$(SED) ${ac_inplace} -e 's/^\.Os$$/.Os Linux/' \
	    $(dist_man_MANS) $(nodist_man_MANS)
endif
