SUBDIRS = zed.d

include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zed

ZED_SRC = \
	zed.c \
	zed.h \
	zed_conf.c \
	zed_conf.h \
	zed_disk_event.c \
	zed_disk_event.h \
	zed_event.c \
	zed_event.h \
	zed_exec.c \
	zed_exec.h \
	zed_file.c \
	zed_file.h \
	zed_log.c \
	zed_log.h \
	zed_strings.c \
	zed_strings.h

FMA_SRC = \
	agents/zfs_agents.c \
	agents/zfs_agents.h \
	agents/zfs_diagnosis.c \
	agents/zfs_mod.c \
	agents/zfs_retire.c \
	agents/fmd_api.c \
	agents/fmd_api.h \
	agents/fmd_serd.c \
	agents/fmd_serd.h

zed_SOURCES = $(ZED_SRC) $(FMA_SRC)

zed_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzfs/libzfs.la

zed_LDADD += -lrt
zed_LDFLAGS = -pthread
