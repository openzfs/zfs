#!/bin/ksh -p

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

# DESCRIPTION:
# 	Verify zpool create with different alloc class redundancy
# 	levels correctly succeed or fail.

verify_runnable "global"

claim="zpool create with different backup and disk permutations work"

log_assert $claim
log_onexit cleanup

# These should always pass since they have same redundancy level
configs_pass="mirror $ZPOOL_DISK1 $ZPOOL_DISK2 special mirror $CLASS_DISK0 $CLASS_DISK1
mirror $ZPOOL_DISK1 $ZPOOL_DISK2 dedup mirror $CLASS_DISK0 $CLASS_DISK1
mirror $ZPOOL_DISK1 $ZPOOL_DISK2 special mirror $CLASS_DISK0 $CLASS_DISK1 dedup mirror $CLASS_DISK2 $CLASS_DISK3"

# These should always pass with backup_to_pool enabled or when '-f' is passed.
# They should fail otherwise.
configs_pass_backup="mirror $ZPOOL_DISK1 $ZPOOL_DISK2 special $CLASS_DISK0
mirror $ZPOOL_DISK1 $ZPOOL_DISK2 dedup  $CLASS_DISK0
mirror $ZPOOL_DISK1 $ZPOOL_DISK2 special $CLASS_DISK0 dedup $CLASS_DISK2
mirror $ZPOOL_DISK1 $ZPOOL_DISK2 special mirror $CLASS_DISK0 $CLASS_DISK1 dedup $CLASS_DISK2"

log_must disk_setup

# Try configs with matching redundancy levels.  They should all pass.
echo "$configs_pass" | while read config ; do
	log_must zpool create -o feature@allow_backup_to_pool=disabled $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_must zpool create -o feature@allow_backup_to_pool=enabled $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_must zpool create -f -o feature@allow_backup_to_pool=disabled $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_must zpool create -f -o feature@allow_backup_to_pool=enabled $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_must zpool create -o feature@allow_backup_to_pool=disabled -o backup_alloc_class_to_pool=off $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_must zpool create -o feature@allow_backup_to_pool=enabled -o backup_alloc_class_to_pool=on $TESTPOOL $config
	log_must zpool destroy $TESTPOOL
done

# Try configs with lower redundancy level.  They should fail if backup to
# pool is turned off and -f is not used.
echo "$configs_pass_backup" | while read config ; do
	log_mustnot zpool create -o feature@allow_backup_to_pool=disabled $TESTPOOL $config

	log_must zpool create -o feature@allow_backup_to_pool=enabled $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_must zpool create -f -o feature@allow_backup_to_pool=disabled $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_must zpool create -f -o feature@allow_backup_to_pool=enabled $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_mustnot zpool create -o feature@allow_backup_to_pool=disabled -o backup_alloc_class_to_pool=off $TESTPOOL $config

	log_must zpool create -f -o feature@allow_backup_to_pool=disabled -o backup_alloc_class_to_pool=off $TESTPOOL $config
	log_must zpool destroy $TESTPOOL

	log_mustnot zpool create -o feature@allow_backup_to_pool=enabled -o backup_alloc_class_to_pool=off $TESTPOOL $config
done

cleanup

log_pass $claim
