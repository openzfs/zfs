SUBDIRS = zpool-config
EXTRA_DIST = common.sh zfs-update.sh zfs.sh zpool-create.sh

check:
	./zfs.sh -v
	./zpios.sh -c file-raid0  -t tiny
	./zpios.sh -c file-raid10 -t tiny
	./zpios.sh -c file-raidz  -t tiny
	./zpios.sh -c file-raidz2 -t tiny
	./zpios.sh -c lo-raid0    -t tiny
	./zpios.sh -c lo-raid10   -t tiny
	./zpios.sh -c lo-raidz    -t tiny
	./zpios.sh -c lo-raidz2   -t tiny
	./zfs.sh -vu
