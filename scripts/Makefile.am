SUBDIRS = zpool-config zpios-test zpios-profile
EXTRA_DIST = common.sh zfs-update.sh zfs.sh zpool-create.sh
EXTRA_DIST += zpios.sh zpios-survey.sh

check:
	./zfs.sh -v
	./zpios.sh -c file-raid0 -t tiny -v
	./zfs.sh -vu
