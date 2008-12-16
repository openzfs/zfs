include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/module/avl/include \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libavl.la

libavl_la_SOURCES = \
	${top_srcdir}/module/avl/avl.c \
	${top_srcdir}/module/avl/include/sys/avl.h \
	${top_srcdir}/module/avl/include/sys/avl_impl.h
