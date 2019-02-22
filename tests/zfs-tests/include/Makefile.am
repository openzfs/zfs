pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/include
dist_pkgdata_DATA = \
	blkdev.shlib \
	commands.cfg \
	libtest.shlib \
	math.shlib \
	properties.shlib \
	zpool_script.shlib

EXTRA_DIST = default.cfg.in

nodist_pkgdata_DATA = default.cfg

$(nodist_pkgdata_DATA): %: %.in
	-$(SED) -e 's,@zfsexecdir\@,$(zfsexecdir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

distclean-local::
	-$(RM) default.cfg
