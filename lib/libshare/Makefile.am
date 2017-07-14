include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libshare.la

USER_C = \
	libshare_impl.h \
	libshare.c \
	nfs.c \
	nfs.h \
	smb.c \
	smb.h

KERNEL_C =

nodist_libshare_la_SOURCES = \
	$(USER_C)
	$(KERNEL_C)

EXTRA_DIST = $(USER_C)
