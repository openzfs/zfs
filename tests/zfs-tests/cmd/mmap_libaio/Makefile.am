include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

if WANT_MMAP_LIBAIO
pkgexec_PROGRAMS = mmap_libaio
mmap_libaio_SOURCES = mmap_libaio.c
mmap_libaio_CFLAGS = $(AM_CFLAGS) $(LIBAIO_CFLAGS)
mmap_libaio_LDADD = $(LIBAIO_LIBS)
endif
