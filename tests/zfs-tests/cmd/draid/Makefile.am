include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

AM_CFLAGS += $(ZLIB_CFLAGS)

pkgexec_PROGRAMS = draid

draid_SOURCES = draid.c

draid_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

draid_LDADD += $(ZLIB_LIBS)
