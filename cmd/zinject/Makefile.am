include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zinject

zinject_SOURCES = \
	translate.c \
	zinject.c \
	zinject.h

zinject_LDADD = \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

include $(top_srcdir)/config/CppCheck.am
