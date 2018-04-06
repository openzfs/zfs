pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/include
dist_pkgdata_DATA = \
	commands.cfg \
	default.cfg \
	libtest.shlib \
	math.shlib \
	properties.shlib \
	zpool_script.shlib

EXTRA_DIST=default.cfg.in

distclean-local::
	-$(RM) default.cfg
