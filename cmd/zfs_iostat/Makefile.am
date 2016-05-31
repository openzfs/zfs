include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zfs_iostat

zfs_iostat_SOURCES = \
	zfs_iostat.c

zfs_iostat_LDADD = \
	$(top_builddir)/lib/libioacct/libioacct.la

zfs_iostat_LDFLAGS = -pthread
