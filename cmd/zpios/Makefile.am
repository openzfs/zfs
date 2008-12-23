include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/module/zpios/include \
	-I${top_srcdir}/module/zfs \
	-I${top_srcdir}/module/zfs/include \
	-I${top_srcdir}/module/zcommon/include

sbin_PROGRAMS = zpios

zpios_SOURCES = \
	$(top_srcdir)/cmd/zpios/zpios.h \
	$(top_srcdir)/cmd/zpios/zpios_main.c \
	$(top_srcdir)/cmd/zpios/zpios_util.c
