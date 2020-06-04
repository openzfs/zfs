include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zfsd

zfsd_SOURCES = \
	callout.cc \
	case_file.cc \
	vdev.cc \
	vdev_iterator.cc \
	zfsd.cc \
	zfsd_event.cc \
	zfsd_exception.cc \
	zfsd_main.cc \
	zpool_list.cc

zfsd_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(top_builddir)/lib/libzfs/libzfs.la

zfsd_LDADD += -lrt -lprivatedevdctl -lgeom -lbsdxml -lsbuf
zfsd_LDFLAGS = -pthread
