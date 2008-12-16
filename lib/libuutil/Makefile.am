include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libuutil/include \
	-I${top_srcdir}/lib/libspl/include \
	-I${top_srcdir}/module/avl/include

lib_LTLIBRARIES = libuutil.la

libuutil_la_SOURCES = \
	${top_srcdir}/lib/libuutil/uu_alloc.c \
	${top_srcdir}/lib/libuutil/uu_avl.c \
	${top_srcdir}/lib/libuutil/uu_dprintf.c \
	${top_srcdir}/lib/libuutil/uu_ident.c \
	${top_srcdir}/lib/libuutil/uu_list.c \
	${top_srcdir}/lib/libuutil/uu_misc.c \
	${top_srcdir}/lib/libuutil/uu_open.c \
	${top_srcdir}/lib/libuutil/uu_pname.c \
	${top_srcdir}/lib/libuutil/uu_strtoint.c \
	${top_srcdir}/lib/libuutil/include/libuutil.h \
	${top_srcdir}/lib/libuutil/include/libuutil_common.h \
	${top_srcdir}/lib/libuutil/include/libuutil_impl.h
