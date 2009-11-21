include $(top_srcdir)/config/rpm.am

if CONFIG_USER
USER_DIR = lib cmd scripts
endif
if CONFIG_KERNEL
KERNEL_DIR = module include
endif
SUBDIRS = $(USER_DIR) $(KERNEL_DIR)

AUTOMAKE_OPTIONS = foreign dist-zip
EXTRA_DIST = autogen.sh spl.spec.in META DISCLAIMER
noinst_HEADERS = spl_config.h spl_unconfig.h

distclean-local::
	-$(RM) -R autom4te*.cache
	-find . \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \
		-o -name .pc -o -name .hg -o -name .git \) -prune -o \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -name 'aclocal.m4' -o -size 0 \
		-o -name '*%' -o -name '.*.cmd' -o -name 'core' \
		-o -name 'Makefile' -o -name '$(LINUX_SYMBOLS)' \
		-o -name '*.order' -o -name '*.markers' \) \
		-type f -print | xargs $(RM)

if CONFIG_KERNEL
install-data-local:
	destname=spl-$(SPL_META_VERSION)/$(LINUX_VERSION); \
	instdest=$(DESTDIR)/${prefix}/src/$$destname; \
	echo "${SPL_META_VERSION}" >$$instdest/spl.release; \
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
