#!/bin/ksh -p

# Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
# Copyright (C) 2024 Lawrence Livermore National Security, LLC.
# Refer to the OpenZFS git commit log for authoritative copyright attribution.
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License Version 1.0 (CDDL-1.0).
# You can obtain a copy of the license from the top-level file
# "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
# You may not use this file except in compliance with the license.
#
# Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049)

. $STF_SUITE/tests/functional/backup_alloc_class/backup_alloc_class.kshlib

#
# DESCRIPTION:
# 	Verify that alloc class backups to pool do not work if
# 	SPA_FEATURE_ALLOW_BACKUP_TO_POOL is disabled.  Also, test upgrades.

verify_runnable "global"

claim="alloc class backups shouldn't work without SPA_FEATURE_ALLOW_BACKUP_TO_POOL"

log_assert $claim
log_onexit cleanup

IMPORTDIR="$(dirname ${CLASS_DISK0})"

# Try a bunch of different pool configurations
configs="$ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1  dedup $CLASS_DISK2 $CLASS_DISK3
raidz $ZPOOL_DISKS special mirror $CLASS_DISK0 $CLASS_DISK1 dedup mirror $CLASS_DISK2 $CLASS_DISK3
$ZPOOL_DISKS special $CLASS_DISK0 dedup $CLASS_DISK1
$ZPOOL_DISKS special $CLASS_DISK0
$ZPOOL_DISKS dedup $CLASS_DISK0"

# Make the pool disks smaller to make them quicker to back up.  We don't use
# much data on them.
export ZPOOL_DEVSIZE=200M
export CLASS_DEVSIZE=200M

log_must disk_setup

echo "$configs" | while read config ; do
	# We should not be able to set backup_alloc_class_to_pool=on if feature
	# flag is disabled.
	log_mustnot zpool create -o feature@allow_backup_to_pool=disabled -o backup_alloc_class_to_pool=on $TESTPOOL $config

	# Try a few permutations that should succeed
	log_must zpool create -o backup_alloc_class_to_pool=off $TESTPOOL $config
	boilerplate_check "active" "off" "off"
	log_must zpool destroy $TESTPOOL

	log_must zpool create -o backup_alloc_class_to_pool=on $TESTPOOL $config
	boilerplate_check "active" "on" "on"
	log_must zpool destroy $TESTPOOL

	log_must zpool create -o feature@allow_backup_to_pool=enabled -o backup_alloc_class_to_pool=on $TESTPOOL $config
	boilerplate_check "active" "on" "on"
	log_must zpool destroy $TESTPOOL

	# Now let's do a multi-step test
	for cmd in "zpool set feature@allow_backup_to_pool=enabled $TESTPOOL" "zpool upgrade $TESTPOOL" ; do
		log_note "config='$config'"
		log_must zpool create -o feature@allow_backup_to_pool=disabled -o backup_alloc_class_to_pool=off $TESTPOOL $config
		totalwritten=0

		boilerplate_check "disabled" "off" "off"
		backup_alloc_class_make_datasets
		write_some_files

		# Test enabling the feature in two different ways:
		#
		#     zpool set allow_backup_to_pool=enabled ...
		#     zpool upgrade ...
		#
		log_must eval "$cmd"
		boilerplate_check "active" "off" "off"
		write_some_files

		log_must zpool set backup_alloc_class_to_pool=on $TESTPOOL
		boilerplate_check "active" "on" "off"
		write_some_files

		log_must zpool export $TESTPOOL
		log_must zpool import -l -d $IMPORTDIR $TESTPOOL

		verify_all_directories

		log_must zpool destroy $TESTPOOL
	done

done

cleanup

log_pass $claim
