include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include

AM_CCASFLAGS = \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libspl.la

libspl_la_SOURCES = \
	${top_srcdir}/lib/libspl/getexecname.c \
	${top_srcdir}/lib/libspl/gethrtime.c \
	${top_srcdir}/lib/libspl/getmntany.c \
	${top_srcdir}/lib/libspl/list.c \
	${top_srcdir}/lib/libspl/mkdirp.c \
	${top_srcdir}/lib/libspl/strlcat.c \
	${top_srcdir}/lib/libspl/strlcpy.c \
	${top_srcdir}/lib/libspl/strnlen.c \
	${top_srcdir}/lib/libspl/u8_textprep.c \
	${top_srcdir}/lib/libspl/zone.c \
	${top_srcdir}/lib/libspl/asm/atomic.S \
	${top_srcdir}/lib/libspl/include/sys/list.h \
	${top_srcdir}/lib/libspl/include/sys/list_impl.h
