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
	$(abs_top_builddir)/libzfs.la \
	$(abs_top_builddir)/libzfs_core.la \
	$(abs_top_builddir)/libnvpair.la \
	$(abs_top_builddir)/libuutil.la

zfs_LDADD += $(LTLIBINTL)

if BUILD_FREEBSD
zfs_LDADD += -lgeom -ljail
endif

include $(top_srcdir)/config/CppCheck.am
CPPCHECKTARGETS += zfs
