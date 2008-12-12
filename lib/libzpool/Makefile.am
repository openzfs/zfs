include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES = \
	-I${top_srcdir}/module/zpool/include \
	-I${top_srcdir}/module/zcommon/include \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libzpool.la

libzpool_la_SOURCES = \
	${top_srcdir}/lib/libzpool/kernel.c
	${top_srcdir}/lib/libzpool/taskq.c
	${top_srcdir}/lib/libzpool/util.c

# FIXME: ADD MISSING SOURCES FROM ZFS MODULE
