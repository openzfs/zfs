pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/cli_root/zpool_import
dist_pkgdata_SCRIPTS = \
	setup.ksh \
	cleanup.ksh \
	zpool_import.kshlib \
	import_cache_device_added.ksh \
	import_cache_device_removed.ksh \
	import_cache_device_replaced.ksh \
	import_cache_mirror_attached.ksh \
	import_cache_mirror_detached.ksh \
	import_cache_shared_device.ksh \
	import_devices_missing.ksh \
	import_paths_changed.ksh \
	import_rewind_config_changed.ksh \
	import_rewind_device_replaced.ksh \
	zpool_import_001_pos.ksh \
	zpool_import_002_pos.ksh \
	zpool_import_003_pos.ksh \
	zpool_import_004_pos.ksh \
	zpool_import_005_pos.ksh \
	zpool_import_006_pos.ksh \
	zpool_import_007_pos.ksh \
	zpool_import_008_pos.ksh \
	zpool_import_009_neg.ksh \
	zpool_import_010_pos.ksh \
	zpool_import_011_neg.ksh \
	zpool_import_012_pos.ksh \
	zpool_import_013_neg.ksh \
	zpool_import_014_pos.ksh \
	zpool_import_015_pos.ksh \
	zpool_import_all_001_pos.ksh \
	zpool_import_features_001_pos.ksh \
	zpool_import_features_002_neg.ksh \
	zpool_import_features_003_pos.ksh \
	zpool_import_missing_001_pos.ksh \
	zpool_import_missing_002_pos.ksh \
	zpool_import_missing_003_pos.ksh \
	zpool_import_rename_001_pos.ksh \
	zpool_import_encrypted.ksh \
	zpool_import_encrypted_load.ksh \
	zpool_import_errata3.ksh

BLOCKFILES = \
	unclean_export.dat.bz2 \
	cryptv0.dat.bz2

dist_pkgdata_DATA = $(BLOCKFILES) \
	zpool_import.cfg

EXTRA_DIST = $(BLOCKFILES)
