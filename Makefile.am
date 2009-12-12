include $(top_srcdir)/config/rpm.am

if CONFIG_USER
USER_DIR = config man scripts lib cmd
endif
if CONFIG_KERNEL
KERNEL_DIR = module
endif
SUBDIRS = $(USER_DIR) $(KERNEL_DIR)

AUTOMAKE_OPTIONS = foreign dist-zip
EXTRA_DIST  = autogen.sh zfs.spec.in zfs-modules.spec.in
EXTRA_DIST += META DISCLAIMER GIT
EXTRA_DIST += OPENSOLARIS.LICENSE ZFS.RELEASE
noinst_HEADERS = zfs_config.h zfs_unconfig.h

distclean-local::
	-$(RM) -R autom4te*.cache
	-find . \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \
		-o -name .pc -o -name .hg -o -name .git \) -prune -o \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -name 'aclocal.m4' -o -size 0 \
		-o -name '*%' -o -name '.*.cmd' -o -name 'core' \
		-o -name 'Makefile' -o -name 'Module.symvers' \
		-o -name '*.order' -o -name '*.markers' \
		-o -name '.script-config' \) \
		-type f -print | xargs $(RM)

if CONFIG_KERNEL
install-data-local:
	destname=zfs-$(ZFS_META_VERSION)/$(LINUX_VERSION); \
	instdest=$(DESTDIR)/${prefix}/src/$$destname; \
	echo "${ZFS_META_VERSION}" >$$instdest/zfs.release; \
	for instfile in $(noinst_HEADERS) module/$(LINUX_SYMBOLS); do \
		$(INSTALL) -D $$instfile $$instdest/$$instfile; \
	done
endif

ctags:
	$(RM) $(top_srcdir)/tags
	find $(top_srcdir) -name .git -prune -o -name '*.[hc]' | xargs ctags

etags:
	$(RM) $(top_srcdir)/TAGS
	find $(top_srcdir) -name .pc -prune -o -name '*.[hc]' | xargs etags -a

tags: ctags etags

srpm-modules:
	$(MAKE) $(AM_MAKEFLAGS) pkg="${PACKAGE}-modules" srpm-common

srpm-utils:
	$(MAKE) $(AM_MAKEFLAGS) pkg="${PACKAGE}" srpm-common

srpm: srpm-modules srpm-utils

rpm-modules: srpm-modules
	$(MAKE) $(AM_MAKEFLAGS) pkg="${PACKAGE}-modules" rpm-common

rpm-utils: srpm-utils
	$(MAKE) $(AM_MAKEFLAGS) pkg="${PACKAGE}" rpm-common

rpm-modules: srpm-modules

rpm-utils: srpm-utils

rpm: rpm-modules rpm-utils
