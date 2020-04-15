
libdiskmgt_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libdiskmgt_la_CFLAGS += -fvisibility=default

libdiskmgt_la_CPPFLAGS  = $(AM_CPPFLAGS)
libdiskmgt_la_CPPFLAGS += -I$(srcdir)/%D%

noinst_LTLIBRARIES += libdiskmgt.la
CPPCHECKTARGETS += libdiskmgt.la

libdiskmgt_la_SOURCES = \
	%D%/dm.c \
	%D%/libdiskmgt.c \
	%D%/diskutil.c \
	%D%/entry.c \
	%D%/inuse_corestorage.c \
	%D%/inuse_fs.c \
	%D%/inuse_macswap.c \
	%D%/inuse_mnt.c \
	%D%/inuse_partition.c \
	%D%/inuse_zpool.c \
	%D%/slice.c

libdiskmgt_la_LDFLAGS = -version-info 1:0:0 -framework DiskArbitration -framework CoreServices
