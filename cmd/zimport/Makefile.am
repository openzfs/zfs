include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zimport

zimport_SOURCES = \
	$(top_srcdir)/cmd/zimport/zimport_main.c

zimport_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzfs/libzfs.la

zimport_LDFLAGS = -pthread -lm $(ZLIB) -lrt -ldl $(LIBUUID) $(LIBBLKID)
