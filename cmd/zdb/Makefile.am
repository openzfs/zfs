include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include

sbin_PROGRAMS = zdb

zdb_SOURCES = \
	zdb.c \
	zdb_il.c

zdb_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la
