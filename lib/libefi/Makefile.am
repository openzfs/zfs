include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libefi/include \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libefi.la

libefi_la_SOURCES = \
	${top_srcdir}/lib/libefi/rdwr_efi.c \
	${top_srcdir}/lib/libefi/include/sys/efi_partition.h \
	${top_srcdir}/lib/libefi/include/sys/uuid.h
