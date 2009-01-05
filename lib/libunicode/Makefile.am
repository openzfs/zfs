include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/module/unicode/include \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libunicode.la

libunicode_la_SOURCES = \
	${top_srcdir}/module/unicode/u8_textprep.c \
	${top_srcdir}/module/unicode/uconv.c \
	${top_srcdir}/module/unicode/include/sys/u8_textprep.h \
	${top_srcdir}/module/unicode/include/sys/u8_textprep_data.h
