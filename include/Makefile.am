# All headers are referenced by this top level Makefile.am are
# noinst_HEADERS because they are not installed in the usual include
# location.  We do not want to be using $includedir for this.
# Installation is handled by the custom install-data-local rule.
noinst_HEADERS  = $(top_srcdir)/include/*.h
noinst_HEADERS += $(top_srcdir)/include/fs/*.h
noinst_HEADERS += $(top_srcdir)/include/linux/*.h
noinst_HEADERS += $(top_srcdir)/include/rpc/*.h
noinst_HEADERS += $(top_srcdir)/include/sharefs/*.h
noinst_HEADERS += $(top_srcdir)/include/sys/fm/*.h
noinst_HEADERS += $(top_srcdir)/include/sys/fs/*.h
noinst_HEADERS += $(top_srcdir)/include/sys/sysevent/*.h
noinst_HEADERS += $(top_srcdir)/include/sys/*.h
noinst_HEADERS += $(top_srcdir)/include/util/*.h
noinst_HEADERS += $(top_srcdir)/include/vm/*.h

install-data-local:
	release=$(SPL_META_VERSION)-$(SPL_META_RELEASE); \
	instdest=$(DESTDIR)/usr/src/spl-$$release/$(LINUX_VERSION); \
	instfiles=`find . -name '*.h'`; \
        for instfile in $$instfiles; do \
		$(INSTALL) -D $$instfile $$instdest/$$instfile; \
        done

uninstall-local:
	release=$(SPL_META_VERSION)-$(SPL_META_RELEASE); \
	instdest=$(DESTDIR)/usr/src/spl-$$release/$(LINUX_VERSION); \
	$(RM) -R $$instdest
