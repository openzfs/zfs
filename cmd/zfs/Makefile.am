sbin_PROGRAMS   += zfs
CPPCHECKTARGETS += zfs

zfs_SOURCES = \
	%D%/zfs_iter.c \
	%D%/zfs_iter.h \
	%D%/zfs_main.c \
	%D%/zfs_project.c \
	%D%/zfs_projectutil.h \
	%D%/zfs_util.h

zfs_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la \
	libuutil.la

zfs_LDADD += $(LTLIBINTL)

if BUILD_FREEBSD
zfs_LDADD += -lgeom -ljail
endif
