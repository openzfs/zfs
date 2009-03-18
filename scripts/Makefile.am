SUBDIRS = zpool-config zpios-test zpios-profile
EXTRA_DIST = common.sh zfs-update.sh zfs.sh zpool-create.sh
EXTRA_DIST += zpios.sh zpios-survey.sh

ZFS=${top_srcdir}/scripts/zfs.sh
ZTEST=${top_builddir}/cmd/ztest/ztest
ZPIOS=${top_srcdir}/scripts/zpios.sh

check:
	$(ZFS) -v
	$(ZTEST) -V
	$(ZPIOS) -c file-raid0  -t tiny
	$(ZPIOS) -c file-raid10 -t tiny
	$(ZPIOS) -c file-raidz  -t tiny
	$(ZPIOS) -c file-raidz2 -t tiny
	$(ZPIOS) -c lo-raid0    -t tiny
	$(ZPIOS) -c lo-raid10   -t tiny
	$(ZPIOS) -c lo-raidz    -t tiny
	$(ZPIOS) -c lo-raidz2   -t tiny
	$(ZFS) -vu
