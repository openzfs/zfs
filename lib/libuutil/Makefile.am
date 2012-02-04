include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libuutil.la

libuutil_la_SOURCES = \
	$(top_srcdir)/lib/libuutil/uu_alloc.c \
	$(top_srcdir)/lib/libuutil/uu_avl.c \
	$(top_srcdir)/lib/libuutil/uu_dprintf.c \
	$(top_srcdir)/lib/libuutil/uu_ident.c \
	$(top_srcdir)/lib/libuutil/uu_list.c \
	$(top_srcdir)/lib/libuutil/uu_misc.c \
	$(top_srcdir)/lib/libuutil/uu_open.c \
	$(top_srcdir)/lib/libuutil/uu_pname.c \
	$(top_srcdir)/lib/libuutil/uu_string.c \
	$(top_srcdir)/lib/libuutil/uu_strtoint.c

libuutil_la_LIBADD = \
	$(top_builddir)/lib/libavl/libavl.la \
	$(top_builddir)/lib/libspl/libspl.la \
	$(top_builddir)/lib/libefi/libefi.la

libuutil_la_LDFLAGS = -pthread -version-info 1:1:0
