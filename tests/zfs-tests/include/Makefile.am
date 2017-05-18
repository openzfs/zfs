pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/include
dist_pkgdata_SCRIPTS = \
	commands.cfg \
	default.cfg \
	libtest.shlib \
	math.shlib \
	properties.shlib \
	zpool_script.shlib

EXTRA_DIST=default.cfg.in

distclean-local::
	-$(RM) $(dist_pkgdata_SCRIPTS)
