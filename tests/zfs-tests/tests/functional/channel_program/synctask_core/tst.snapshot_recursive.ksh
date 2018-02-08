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
# Copyright (c) 2016, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION: Construct a set of nested filesystems, then recursively snapshot
# all of them.
#

verify_runnable "global"

rootfs=$TESTPOOL/$TESTFS/root
snapname=snap

function cleanup
{
	datasetexists $rootfs && log_must zfs destroy -R $rootfs
}

log_onexit cleanup

filesystems="$rootfs \
$rootfs/child1 \
$rootfs/child1/subchild1 \
$rootfs/child1/subchild2 \
$rootfs/child1/subchild3 \
$rootfs/child2 \
$rootfs/child2/subchild4 \
$rootfs/child2/subchild5"

for fs in $filesystems; do
    log_must zfs create $fs
done

log_must_program_sync $TESTPOOL \
    $ZCP_ROOT/synctask_core/tst.snapshot_recursive.zcp $rootfs $snapname

#
# Make sure all of the snapshots we expect were created.
#
for fs in $filesystems; do
    log_must snapexists $fs@$snapname
done

log_pass "Recursively snapshotting multiple filesystems works."


