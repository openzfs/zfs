include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libshare.la

libshare_la_SOURCES = \
	$(top_srcdir)/lib/libshare/libshare.c \
	$(top_srcdir)/lib/libshare/nfs.c \
	$(top_srcdir)/lib/libshare/libshare_impl.h \
	$(top_srcdir)/lib/libshare/nfs.h

libshare_la_LDFLAGS = -version-info 1:0:0
