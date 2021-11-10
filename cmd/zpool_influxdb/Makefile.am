include $(top_srcdir)/config/Rules.am

zfsexec_PROGRAMS = zpool_influxdb

zpool_influxdb_SOURCES = \
	zpool_influxdb.c

zpool_influxdb_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzfs/libzfs.la

include $(top_srcdir)/config/CppCheck.am
