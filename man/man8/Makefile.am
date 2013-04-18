man_MANS = \
	fsck.zfs.8 \
	mount.zfs.8 \
	vdev_id.8 \
	zdb.8 \
	zfs.8 \
	zinject.8 \
	zpool.8 \
	zstreamdump.8

EXTRA_DIST = $(man_MANS)

install-data-local:
	$(INSTALL) -d -m 0755 "$(DESTDIR)$(mandir)/man8"
