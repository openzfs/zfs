include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/module/nvpair/include \
	-I${top_srcdir}/lib/libnvpair/include \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libnvpair.la

libnvpair_la_SOURCES = \
	${top_srcdir}/lib/libnvpair/libnvpair.c \
	${top_srcdir}/lib/libnvpair/nvpair_alloc_system.c \
	${top_srcdir}/lib/libnvpair/include/libnvpair.h \
	${top_srcdir}/module/nvpair/nvpair.c \
	${top_srcdir}/module/nvpair/include/sys/nvpair.h \
	${top_srcdir}/module/nvpair/include/sys/nvpair_impl.h
