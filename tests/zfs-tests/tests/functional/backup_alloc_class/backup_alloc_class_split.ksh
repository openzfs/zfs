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

#
# DESCRIPTION:
# 	Verify we can split a pool with backup to pool, and the new pool
# 	keeps the backup to pool settings.  Also verify the new pool has
# 	all the data if the pool is backed up.
#
verify_runnable "global"

claim="zpool split works with backup to pool"

log_assert $claim
log_onexit cleanup

IMPORTDIR="$(dirname ${CLASS_DISK0})"


# Create a normal, backed-up pool
log_must disk_setup
log_must zpool create -o backup_alloc_class_to_pool=on $TESTPOOL mirror \
    $ZPOOL_DISK0 $ZPOOL_DISK1 special mirror $CLASS_DISK0 $CLASS_DISK1 dedup \
    mirror $CLASS_DISK2 $CLASS_DISK3

totalwritten=0
backup_alloc_class_make_datasets
write_some_files
verify_all_directories

# Split the pool and verify the old pool has all the data
newpool="${TESTPOOL}-2"

log_must zpool split $TESTPOOL $newpool
check_backup_to_pool_is "on"
check_pool_alloc_class_props
verify_all_directories

# Forcefault alloc class devices on the old pool and verify we have all the
# data.
log_must zpool offline -f $TESTPOOL $CLASS_DISK0
log_must zpool offline -f $TESTPOOL $CLASS_DISK2
log_must check_state $TESTPOOL $CLASS_DISK0 "FAULTED"
log_must check_state $TESTPOOL $CLASS_DISK2 "FAULTED"

log_must check_state $TESTPOOL "" "DEGRADED"
verify_all_directories

log_must zpool clear $TESTPOOL

# All done with the old pool
log_must zpool destroy $TESTPOOL

# Import the new split pool and rename it $TESTPOOL since all our verification
# functions expect the pool to be called $TESTPOOL.
log_must zpool import -l -f -d $IMPORTDIR $newpool $TESTPOOL

check_backup_to_pool_is "on"
check_pool_alloc_class_props
verify_all_directories

# zero alloc class devices on the old pool and verify we have all the
# data.
log_must zpool export $TESTPOOL

zero_file $CLASS_DISK1
zero_file $CLASS_DISK3

log_must zpool import -l -f -d $IMPORTDIR $TESTPOOL

verify_all_directories
log_must zpool destroy $TESTPOOL

# Create a non-backed-up pool, split it, and verify the split pool is also
# not backed-up.
log_must zpool create -o backup_alloc_class_to_pool=off $TESTPOOL mirror \
    $ZPOOL_DISK0 $ZPOOL_DISK1 special mirror $CLASS_DISK0 $CLASS_DISK1 dedup \
    mirror $CLASS_DISK2 $CLASS_DISK3

log_must zpool split $TESTPOOL $newpool
check_backup_to_pool_is "off"
check_pool_alloc_class_props
log_must zpool destroy $TESTPOOL
log_must zpool import -l -f -d $IMPORTDIR $newpool $TESTPOOL
check_backup_to_pool_is "off"
check_pool_alloc_class_props
log_must zpool destroy $TESTPOOL

log_pass $claim
