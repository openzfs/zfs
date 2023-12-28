#!/bin/ksh -p

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017, Intel Corporation.
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Creating a pool fails when an invalid special device type is specified.
#
claim="Creating a pool with an invalid special device type fails."

verify_runnable "global"

log_assert $claim
log_onexit cleanup

log_must disk_setup

# Test with the older mode where there was no allow_backup_to_pool
# feature.  With this configuration, the special device redundancy needs
# to match the pool.
arg='-o feature@allow_backup_to_pool=disabled'
for atype in "special" "dedup" ; do
    log_mustnot zpool create $arg $TESTPOOL raidz $ZPOOL_DISKS $atype $CLASS_DISK0

    log_mustnot zpool create $arg $TESTPOOL $ZPOOL_DISKS $atype mirror \
        $CLASS_DISK0 $CLASS_DISK1

    log_mustnot zpool create $arg $TESTPOOL raidz $ZPOOL_DISKS $atype raidz \
        $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2

    # Now test with backup_allocation_classes=enabled (default setting)
    log_must zpool create $TESTPOOL raidz $ZPOOL_DISKS $atype $CLASS_DISK0
    log_must zpool destroy $TESTPOOL

    log_must zpool create $TESTPOOL $ZPOOL_DISKS $atype mirror \
        $CLASS_DISK0 $CLASS_DISK1

    log_must zpool destroy $TESTPOOL

    log_mustnot zpool create $TESTPOOL raidz $ZPOOL_DISKS $atype raidz \
        $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2

done

log_pass $claim
