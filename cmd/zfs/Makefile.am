include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zfs

zfs_SOURCES = \
	zfs_iter.c \
	zfs_iter.h \
	zfs_main.c \
	zfs_util.h \
	zfs_project.c \
	zfs_projectutil.h

zfs_LDADD = \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libuutil/libuutil.la

zfs_LDADD += $(LTLIBINTL)

if BUILD_FREEBSD
zfs_LDADD += -lgeom -ljail
endif
