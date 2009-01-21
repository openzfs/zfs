SUBDIRS = zpool-config
EXTRA_DIST = common.sh zfs-update.sh zfs.sh zpool-create.sh

check:
	./zfs.sh -v
	./zpios.sh -c file-raid0 -t tiny -v
	./zfs.sh -vu
