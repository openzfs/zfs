include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libshare.la

libshare_la_SOURCES = \
	$(top_srcdir)/lib/libshare/libshare_impl.h \
	$(top_srcdir)/lib/libshare/libshare.c \
	$(top_srcdir)/lib/libshare/nfs.c \
	$(top_srcdir)/lib/libshare/nfs.h \
	$(top_srcdir)/lib/libshare/smb.c \
	$(top_srcdir)/lib/libshare/smb.h \
	$(top_srcdir)/lib/libshare/iscsi.c \
	$(top_srcdir)/lib/libshare/iscsi_iet.c \
	$(top_srcdir)/lib/libshare/iscsi_lio.c \
	$(top_srcdir)/lib/libshare/iscsi_scst.c \
	$(top_srcdir)/lib/libshare/iscsi_stgt.c \
	$(top_srcdir)/lib/libshare/iscsi.h

libshare_la_LIBADD = \
	$(top_builddir)/lib/libspl/libspl.la
