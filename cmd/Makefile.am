SUBDIRS  = zfs zpool zdb zhack zinject zstream zstreamdump ztest
# BUGBUG raidz expansion no raidz_test for now
#SUBDIRS += fsck_zfs vdev_id raidz_test zfs_ids_to_path
SUBDIRS += fsck_zfs vdev_id zfs_ids_to_path
# BUGBUG raidz expansion end

if USING_PYTHON
SUBDIRS += arcstat arc_summary dbufstat
endif

if BUILD_LINUX
SUBDIRS += mount_zfs zed zgenhostid zvol_id zvol_wait
endif
