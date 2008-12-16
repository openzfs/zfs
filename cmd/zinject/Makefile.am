include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include

sbin_PROGRAMS = zinject

zinject_SOURCES = \
	translate.c \
	zinject.ci \
	zinject.h

zinject_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la
