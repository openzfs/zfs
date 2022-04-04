include $(top_srcdir)/config/Substfiles.am

EXTRA_DIST += \
	man1/cstyle.1

dist_man_MANS = \
	man1/zhack.1 \
	man1/ztest.1 \
	man1/raidz_test.1 \
	man1/zvol_wait.1 \
	man1/arcstat.1 \
	\
	man5/vdev_id.conf.5 \
	\
	man4/spl.4 \
	man4/zfs.4 \
	\
	man7/dracut.zfs.7 \
	man7/zpool-features.7 \
	man7/zfsconcepts.7 \
	man7/zfsprops.7 \
	man7/zpoolconcepts.7 \
	man7/zpoolprops.7 \
	\
	man8/fsck.zfs.8 \
	man8/mount.zfs.8 \
	man8/vdev_id.8 \
	man8/zdb.8 \
	man8/zfs.8 \
	man8/zfs-allow.8 \
	man8/zfs-bookmark.8 \
	man8/zfs-change-key.8 \
	man8/zfs-clone.8 \
	man8/zfs-create.8 \
	man8/zfs-destroy.8 \
	man8/zfs-diff.8 \
	man8/zfs-get.8 \
	man8/zfs-groupspace.8 \
	man8/zfs-hold.8 \
	man8/zfs-inherit.8 \
	man8/zfs-jail.8 \
	man8/zfs-list.8 \
	man8/zfs-load-key.8 \
	man8/zfs-mount.8 \
	man8/zfs-program.8 \
	man8/zfs-project.8 \
	man8/zfs-projectspace.8 \
	man8/zfs-promote.8 \
	man8/zfs-receive.8 \
	man8/zfs-recv.8 \
	man8/zfs-redact.8 \
	man8/zfs-release.8 \
	man8/zfs-rename.8 \
	man8/zfs-rollback.8 \
	man8/zfs-send.8 \
	man8/zfs-set.8 \
	man8/zfs-share.8 \
	man8/zfs-snapshot.8 \
	man8/zfs-unallow.8 \
	man8/zfs-unjail.8 \
	man8/zfs-unload-key.8 \
	man8/zfs-unmount.8 \
	man8/zfs-upgrade.8 \
	man8/zfs-userspace.8 \
	man8/zfs-wait.8 \
	man8/zfs_ids_to_path.8 \
	man8/zgenhostid.8 \
	man8/zinject.8 \
	man8/zpool.8 \
	man8/zpool-add.8 \
	man8/zpool-attach.8 \
	man8/zpool-checkpoint.8 \
	man8/zpool-clear.8 \
	man8/zpool-create.8 \
	man8/zpool-destroy.8 \
	man8/zpool-detach.8 \
	man8/zpool-events.8 \
	man8/zpool-export.8 \
	man8/zpool-get.8 \
	man8/zpool-history.8 \
	man8/zpool-import.8 \
	man8/zpool-initialize.8 \
	man8/zpool-iostat.8 \
	man8/zpool-labelclear.8 \
	man8/zpool-list.8 \
	man8/zpool-offline.8 \
	man8/zpool-online.8 \
	man8/zpool-reguid.8 \
	man8/zpool-remove.8 \
	man8/zpool-reopen.8 \
	man8/zpool-replace.8 \
	man8/zpool-resilver.8 \
	man8/zpool-scrub.8 \
	man8/zpool-set.8 \
	man8/zpool-split.8 \
	man8/zpool-status.8 \
	man8/zpool-sync.8 \
	man8/zpool-trim.8 \
	man8/zpool-upgrade.8 \
	man8/zpool-wait.8 \
	man8/zstream.8 \
	man8/zstreamdump.8 \
	man8/zpool_influxdb.8

nodist_man_MANS = \
	man8/zed.8 \
	man8/zfs-mount-generator.8

SUBSTFILES += $(nodist_man_MANS)


if BUILD_LINUX
# The manual pager in most Linux distros defaults to "BSD" when .Os is blank,
# but leaving it blank makes things a lot easier on
# FreeBSD when OpenZFS is vendored in the base system.
install-data-hook:
	cd $(DESTDIR)$(mandir) && $(SED) ${ac_inplace} -e 's/^\.Os$$/.Os OpenZFS/' $(dist_man_MANS) $(nodist_man_MANS)
endif
