include $(top_srcdir)/config/Shellcheck.am

SUBDIRS  = zfs zpool zdb zhack zinject zstream ztest
SUBDIRS += fsck_zfs vdev_id raidz_test zfs_ids_to_path
SUBDIRS += zpool_influxdb

CPPCHECKDIRS  = zfs zpool zdb zhack zinject zstream ztest
CPPCHECKDIRS += raidz_test zfs_ids_to_path zpool_influxdb

# TODO: #12084: SHELLCHECKDIRS += vdev_id
SHELLCHECKDIRS = fsck_zfs zed zpool zvol_wait
SHELLCHECK_OPTS = --enable=all

if USING_PYTHON
SUBDIRS += arcstat arc_summary dbufstat
endif

if BUILD_LINUX
SUBDIRS += mount_zfs zed zgenhostid zvol_id zvol_wait
CPPCHECKDIRS += mount_zfs zed zgenhostid zvol_id
SHELLCHECKDIRS += zed
endif

PHONY = cppcheck
cppcheck: $(CPPCHECKDIRS)
	set -e ; for dir in $(CPPCHECKDIRS) ; do \
		$(MAKE) -C $$dir cppcheck ; \
	done
