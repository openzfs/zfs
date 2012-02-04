include $(top_srcdir)/config/rpm.am
include $(top_srcdir)/config/deb.am
include $(top_srcdir)/config/tgz.am
include $(top_srcdir)/config/arch.am

if CONFIG_USER
USER_DIR = dracut udev etc man scripts lib cmd
endif
if CONFIG_KERNEL
KERNEL_DIR = module
endif
SUBDIRS = $(USER_DIR) $(KERNEL_DIR) include

AUTOMAKE_OPTIONS = foreign
EXTRA_DIST  = autogen.sh zfs.spec.in zfs-modules.spec.in
EXTRA_DIST += config/config.awk config/rpm.am config/deb.am config/tgz.am
EXTRA_DIST += META DISCLAIMER COPYRIGHT README.markdown
EXTRA_DIST += OPENSOLARIS.LICENSE ZFS.RELEASE

noinst_HEADERS = zfs_config.h

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

if CONFIG_KERNEL
install-data-local:
	release=$(ZFS_META_VERSION)-$(ZFS_META_RELEASE); \
	instdest=$(DESTDIR)/usr/src/zfs-$$release/$(LINUX_VERSION); \
	echo "$$release" >$$instdest/zfs.release; \
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

pkg: @DEFAULT_PACKAGE@
pkg-modules: @DEFAULT_PACKAGE@-modules
pkg-utils: @DEFAULT_PACKAGE@-utils
