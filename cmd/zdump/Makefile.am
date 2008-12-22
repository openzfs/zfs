include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include

sbin_PROGRAMS = zdump

zdump_SOURCES = \
	${top_srcdir}/cmd/zdump/zdump.c

zdump_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la
