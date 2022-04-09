bin_SCRIPTS =
bin_PROGRAMS =
sbin_SCRIPTS =
sbin_PROGRAMS =
udev_PROGRAMS =
dist_bin_SCRIPTS =
zfsexec_PROGRAMS =
dist_udev_SCRIPTS =
mounthelper_PROGRAMS =

include $(srcdir)/%D%/fsck_zfs/Makefile.am
include $(srcdir)/%D%/raidz_test/Makefile.am
include $(srcdir)/%D%/vdev_id/Makefile.am
include $(srcdir)/%D%/zdb/Makefile.am
include $(srcdir)/%D%/zfs/Makefile.am
include $(srcdir)/%D%/zfs_ids_to_path/Makefile.am
include $(srcdir)/%D%/zhack/Makefile.am
include $(srcdir)/%D%/zinject/Makefile.am
include $(srcdir)/%D%/zpool/Makefile.am
include $(srcdir)/%D%/zpool_influxdb/Makefile.am
include $(srcdir)/%D%/zstream/Makefile.am
include $(srcdir)/%D%/ztest/Makefile.am

if BUILD_LINUX
include $(srcdir)/%D%/mount_zfs/Makefile.am
include $(srcdir)/%D%/zed/Makefile.am
include $(srcdir)/%D%/zgenhostid/Makefile.am
include $(srcdir)/%D%/zvol_id/Makefile.am
include $(srcdir)/%D%/zvol_wait/Makefile.am
endif

if USING_PYTHON
include $(srcdir)/%D%/arc_summary/Makefile.am
include $(srcdir)/%D%/arcstat/Makefile.am
include $(srcdir)/%D%/dbufstat/Makefile.am
endif

PHONY += cmd
cmd: $(bin_SCRIPTS) $(bin_PROGRAMS) $(sbin_SCRIPTS) $(sbin_PROGRAMS) $(udev_PROGRAMS) $(dist_bin_SCRIPTS) $(zfsexec_PROGRAMS) $(dist_udev_SCRIPTS) $(mounthelper_PROGRAMS)
