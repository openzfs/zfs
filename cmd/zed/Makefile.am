include $(srcdir)/%D%/zed.d/Makefile.am

zed_CFLAGS  = $(AM_CFLAGS)
zed_CFLAGS += $(LIBUDEV_CFLAGS) $(LIBUUID_CFLAGS)

sbin_PROGRAMS   += zed
CPPCHECKTARGETS += zed

zed_SOURCES = \
	%D%/zed.c \
	%D%/zed.h \
	%D%/zed_conf.c \
	%D%/zed_conf.h \
	%D%/zed_disk_event.c \
	%D%/zed_disk_event.h \
	%D%/zed_event.c \
	%D%/zed_event.h \
	%D%/zed_exec.c \
	%D%/zed_exec.h \
	%D%/zed_file.c \
	%D%/zed_file.h \
	%D%/zed_log.c \
	%D%/zed_log.h \
	%D%/zed_strings.c \
	%D%/zed_strings.h \
	\
	%D%/agents/fmd_api.c \
	%D%/agents/fmd_api.h \
	%D%/agents/fmd_serd.c \
	%D%/agents/fmd_serd.h \
	%D%/agents/zfs_agents.c \
	%D%/agents/zfs_agents.h \
	%D%/agents/zfs_diagnosis.c \
	%D%/agents/zfs_mod.c \
	%D%/agents/zfs_retire.c

zed_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la \
	libuutil.la

zed_LDADD += -lrt $(LIBATOMIC_LIBS) $(LIBUDEV_LIBS) $(LIBUUID_LIBS)
zed_LDFLAGS = -pthread

dist_noinst_DATA += %D%/agents/README.md
