include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include

sbin_PROGRAMS = zdump

zdump_SOURCES = \
	zdump.c

zdump_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la
