libshare_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libshare_la_CFLAGS += -fvisibility=hidden

libshare_la_CPPFLAGS  = $(AM_CPPFLAGS)
libshare_la_CPPFLAGS += -I$(srcdir)/%D%

noinst_LTLIBRARIES += libshare.la
CPPCHECKTARGETS    += libshare.la

libshare_la_SOURCES = \
	%D%/libshare_impl.h \
	%D%/libshare.c \
	%D%/nfs.c \
	%D%/nfs.h \
	%D%/smb.h

if BUILD_LINUX
libshare_la_SOURCES += \
	%D%/os/linux/nfs.c \
	%D%/os/linux/smb.c
endif

if BUILD_FREEBSD
libshare_la_SOURCES += \
	%D%/os/freebsd/nfs.c \
	%D%/os/freebsd/smb.c
endif
