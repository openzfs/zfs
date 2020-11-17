dist_man_MANS = zhack.1 ztest.1 raidz_test.1 zvol_wait.1 arcstat.1
EXTRA_DIST = cstyle.1

if BUILD_LINUX
# The man pager in most Linux distros defaults to BSD instead of Linux
# when .Os is blank, but leaving it blank makes things a lot easier on
# FreeBSD when OpenZFS is vendored in the base system.
install-data-hook:
	cd $(DESTDIR)$(mandir)/man1; \
	$(SED) ${ac_inplace} -e 's/^\.Os$$/.Os Linux/' \
	    $(dist_man_MANS)
endif
