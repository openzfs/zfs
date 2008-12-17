include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include \
	-I${top_srcdir}/lib/libzpool/include \
	-I${top_srcdir}/lib/libnvpair/include \
	-I${top_srcdir}/lib/libzfs/include \
	-I${top_srcdir}/module/zfs/include \
	-I${top_srcdir}/module/zcommon/include \
	-I${top_srcdir}/module/nvpair/include \
	-I${top_srcdir}/module/avl/include

sbin_PROGRAMS = zdb

zdb_SOURCES = \
	zdb.c \
	zdb_il.c

zdb_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la
