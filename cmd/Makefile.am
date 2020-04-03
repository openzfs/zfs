SUBDIRS  = zfs zpool zdb zhack zinject zstreamdump ztest
# BUGBUG raidz expansion no raidz_test for now
#SUBDIRS += fsck_zfs vdev_id raidz_test zgenhostid
SUBDIRS += fsck_zfs vdev_id zgenhostid
# BUGBUG raidz expansion end

if USING_PYTHON
SUBDIRS += arcstat arc_summary dbufstat
endif

if BUILD_LINUX
SUBDIRS += mount_zfs zed zvol_id zvol_wait
endif
