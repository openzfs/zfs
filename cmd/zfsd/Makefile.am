zfsd_CFLAGS = $(AM_CFLAGS)
zfsd_CXXFLAGS = $(AM_CXXFLAGS)
zfsd_CPPFLAGS = $(AM_CPPFLAGS)

sbin_PROGRAMS += zfsd

zfsd_SOURCES = \
	%D%/callout.cc \
	%D%/case_file.cc \
	%D%/vdev.cc \
	%D%/vdev_iterator.cc \
	%D%/zfsd.cc \
	%D%/zfsd_event.cc \
	%D%/zfsd_exception.cc \
	%D%/zfsd_main.cc \
	%D%/zpool_list.cc

zfsd_LDADD = \
	libnvpair.la \
	libuutil.la \
	libzfs_core.la \
	libzfs.la

zfsd_LDADD += -lrt -lprivatedevdctl -lgeom -lbsdxml -lsbuf
zfsd_LDFLAGS = -pthread
