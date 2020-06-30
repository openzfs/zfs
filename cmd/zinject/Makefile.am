include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zinject

zinject_SOURCES = \
	translate.c \
	zinject.c \
	zinject.h

zinject_LDADD = \
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la
