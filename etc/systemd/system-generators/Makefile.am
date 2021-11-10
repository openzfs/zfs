include $(top_srcdir)/config/Rules.am

systemdgenerator_PROGRAMS = \
	zfs-mount-generator

zfs_mount_generator_SOURCES = \
	zfs-mount-generator.c

zfs_mount_generator_LDADD = \
	$(abs_top_builddir)/lib/libzfs/libzfs.la

zfs_mount_generator_LDFLAGS = -pthread

include $(top_srcdir)/config/CppCheck.am
