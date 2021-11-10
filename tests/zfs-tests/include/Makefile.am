include $(top_srcdir)/config/Substfiles.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/include
dist_pkgdata_DATA = \
	blkdev.shlib \
	commands.cfg \
	libtest.shlib \
	math.shlib \
	properties.shlib \
	tunables.cfg \
	zpool_script.shlib

nodist_pkgdata_DATA = default.cfg
SUBSTFILES += $(nodist_pkgdata_DATA)
