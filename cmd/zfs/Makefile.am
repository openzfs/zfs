include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include

sbin_PROGRAMS = zfs

zfs_SOURCES = \
	zfs_iter.c \
	zfs_iter.h \
	zfs_main.c \
	zfs_util.h

zfs_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la
