include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include


sbin_PROGRAMS = zpios

zpios_SOURCES = \
	$(top_srcdir)/cmd/zpios/zpios_main.c \
	$(top_srcdir)/cmd/zpios/zpios_util.c \
	$(top_srcdir)/cmd/zpios/zpios.h
zpios_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la

