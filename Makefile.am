
ACLOCAL_AMFLAGS = -I config

include $(top_srcdir)/config/rpm.am
include $(top_srcdir)/config/deb.am
include $(top_srcdir)/config/tgz.am

SUBDIRS = include rpm
if CONFIG_USER
SUBDIRS += dracut udev etc man scripts lib cmd
endif
if CONFIG_KERNEL
SUBDIRS += module

extradir = /usr/src/zfs-$(VERSION)
extra_HEADERS = zfs.release.in zfs_config.h.in

kerneldir = /usr/src/zfs-$(VERSION)/$(LINUX_VERSION)
nodist_kernel_HEADERS = zfs.release zfs_config.h module/$(LINUX_SYMBOLS)
endif

AUTOMAKE_OPTIONS = foreign
EXTRA_DIST  = autogen.sh copy-builtin
EXTRA_DIST += config/config.awk config/rpm.am config/deb.am config/tgz.am
EXTRA_DIST += META DISCLAIMER COPYRIGHT README.markdown OPENSOLARIS.LICENSE

distclean-local::
	-$(RM) -R autom4te*.cache
	-find . \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \
		-o -name .pc -o -name .hg -o -name .git \) -prune -o \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -name '.script-config' -o -size 0 \
		-o -name '*%' -o -name '.*.cmd' -o -name 'core' \
		-o -name 'Makefile' -o -name 'Module.symvers' \
		-o -name '*.order' -o -name '*.markers' \) \
		-type f -print | xargs $(RM)

dist-hook:
	sed -i 's/Release:[[:print:]]*/Release:      $(RELEASE)/' \
		$(distdir)/META

checkstyle:
	@find ${top_srcdir} -name '*.[hc]' ! -name 'zfs_config.*' \
		! -name '*.mod.c' -type f -exec scripts/cstyle.pl {} \+

ctags:
	$(RM) $(top_srcdir)/tags
	find $(top_srcdir) -name .git -prune -o -name '*.[hc]' | xargs ctags

etags:
	$(RM) $(top_srcdir)/TAGS
	find $(top_srcdir) -name .pc -prune -o -name '*.[hc]' | xargs etags -a

test: test_verify test/zfs-tests/tests/functional/ctime/ctime_001_pos test/zfs-tests/cmd
	@SRC=$(abs_top_srcdir) \
	 STF_TOOLS=$(abs_top_srcdir)/test/test-runner/stf \
	 STF_SUITE=$(abs_top_srcdir)/test/zfs-tests \
	 DISKS="$(shell for i in {1..3}; do \
	    $(abs_top_srcdir)/scripts/mkfile -s 5000m /var/tmp/zfs_test-$$i; \
	    echo "/var/tmp/zfs_test-$$i "; \
	   done)" \
	 su zfs-tests -c "ksh $(abs_top_srcdir)/test/zfs-tests/cmd/scripts/zfstest.ksh $$RUNFILE"

test_verify:
	@# -------------------------------------------------------------------
	@# adduser --home /var/tmp/test_results --shell /usr/bin/ksh --gecos "ZFS Test Suite" --disabled-password zfs-tests
	@# echo 'zfs-tests	ALL = NOPASSWD: ALL' >> /etc/sudoers
	@# mkdir /opt && cd /opt
	@# ln -s $(top_srcdir)/test/test-runner .
	@# ln -s $(top_srcdir)/test/zfs-tests .
	@# -------------------------------------------------------------------
	@( if ! id zfs-tests > /dev/null 2>&1; then \
		echo "ERROR: You must create a zfs-test user to run the test as."; \
		exit 1; \
	   fi; \
	   if [ ! -L /opt/test-runner ]; then \
		echo "ERROR: You will need to create the /opt/test-runner link"; \
		echo "       pointing to `pwd`/test/test-runner."; \
		exit 1; \
	   fi; \
	   if [ ! -L /opt/zfs-tests ]; then \
		echo "ERROR: You will need to create the /opt/test-runner link"; \
		echo "       pointing to `pwd`/test/zfs-tests."; \
		exit 1; \
	   fi; \
	   if [ `zpool list -H | wc -l` -gt 0 ]; then \
		echo "ERROR: You can't be running a pool - it will be destroyed!"; \
		exit 1; \
	   fi)

test/zfs-tests/tests/functional/ctime/ctime_001_pos:
	make -C test/zfs-tests/tests/functional/ctime -f Makefile.linux

test/zfs-tests/cmd:
	make -C test/zfs-tests/cmd/chg_usr_exec -f Makefile.linux
	make -C test/zfs-tests/cmd/mktree -f Makefile.linux
	make -C test/zfs-tests/cmd/file_write -f Makefile.linux
	make -C test/zfs-tests/cmd/mkbusy -f Makefile.linux
	make -C test/zfs-tests/tests/functional/ctime -f Makefile.linux

tags: ctags etags

pkg: @DEFAULT_PACKAGE@
pkg-kmod: @DEFAULT_PACKAGE@-kmod
pkg-utils: @DEFAULT_PACKAGE@-utils

.PHONY: test
