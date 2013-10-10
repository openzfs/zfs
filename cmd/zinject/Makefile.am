include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zinject

zinject_SOURCES = \
	$(top_srcdir)/cmd/zinject/translate.c \
	$(top_srcdir)/cmd/zinject/zinject.c \
	$(top_srcdir)/cmd/zinject/zinject.h

zinject_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la
