pkgpyzfsdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/pyzfs
pkgpyzfs_SCRIPTS = \
	pyzfs_unittest.ksh

EXTRA_DIST = \
	pyzfs_unittest.ksh.in

#
# The pyzfs module is built either for Python 2 or Python 3.  In order
# to properly test it the unit tests must be updated to the matching version.
#
$(pkgpyzfs_SCRIPTS):%:%.in
	-$(SED) -e 's,@PYTHON\@,$(PYTHON),g' \
		$< >'$@'
	-chmod 775 $@

distclean-local::
	-$(RM) $(pkgpyzfs_SCRIPTS)
