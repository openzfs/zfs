include $(top_srcdir)/config/Rules.am

zfsexec_PROGRAMS = zpool_influxdb

zpool_influxdb_SOURCES = \
	zpool_influxdb.c

zpool_influxdb_LDADD = \
	$(top_builddir)/libspl.la \
	$(top_builddir)/libnvpair.la \
	$(top_builddir)/libzfs.la

include $(top_srcdir)/config/CppCheck.am
CPPCHECKTARGETS += zpool_influxdb
