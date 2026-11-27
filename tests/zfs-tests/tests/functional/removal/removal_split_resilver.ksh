#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026 by Oxide Computer Company. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# A resilver gives new devices the split blocks of a removed vdev.  Without
# checksums, it doesn't overwrite a copy that it can't read with one that it
# can't verify.
#
# STRATEGY:
# 1. Create a pool, write a file, add two mirrors and remove the first vdev
#    with 8K removal segments, so that every 128K block of the file is
#    split into 16 segments, on both mirrors.
# 2. Replace a child of each mirror with a new vdev, so that one resilver
#    is for both.
# 3. Re-import the pool with the other children offline, so that the new
#    vdevs hold the only copies of the file's segments.  Check the file,
#    scrub and check for errors.
# 4. Create such a pool without checksums, with the file's segments on a
#    single disk, and the metadata on a special vdev so that it stays
#    readable.
# 5. Attach a second disk to the first while reads of the first fail.  The
#    resilver can only read the new disk's copy of each segment, which is
#    missing the data according to its DTL.
# 6. Detach the new disk, and check that the first disk still has the file.
#
verify_runnable "global"

typeset VDEV_DIR=$TEST_BASE_DIR/removal_split_resilver
typeset VDEV_A=$VDEV_DIR/a
typeset VDEV_B1=$VDEV_DIR/b1
typeset VDEV_B2=$VDEV_DIR/b2
typeset VDEV_C1=$VDEV_DIR/c1
typeset VDEV_C2=$VDEV_DIR/c2
typeset VDEV_NEW1=$VDEV_DIR/new1
typeset VDEV_NEW2=$VDEV_DIR/new2
typeset VDEV_SPECIAL=$VDEV_DIR/special

function cleanup
{
	zinject -c all
	log_must set_tunable32 REMOVE_MAX_SEGMENT $ORIG_REMOVE_MAX_SEGMENT
	log_must set_tunable64 METASLAB_ALIQUOT $ORIG_METASLAB_ALIQUOT
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR
}

function setup_vdevs
{
	log_must mkdir -p $VDEV_DIR
	log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B1 $VDEV_B2 \
	    $VDEV_C1 $VDEV_C2 $VDEV_NEW1 $VDEV_NEW2 $VDEV_SPECIAL
}

log_assert "A resilver gives new devices the split blocks of a removed vdev"

ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
ORIG_METASLAB_ALIQUOT=$(get_tunable METASLAB_ALIQUOT)
ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
log_onexit cleanup

log_note "Replace a child of each of two mirrors at once"
setup_vdevs
SPLIT_POOL_SPREAD=1
create_split_pool $VDEV_A mirror $VDEV_B1 $VDEV_B2 mirror $VDEV_C1 $VDEV_C2
SPLIT_POOL_SPREAD=""
log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace $TESTPOOL $VDEV_B2 $VDEV_NEW1
log_must zpool replace $TESTPOOL $VDEV_C2 $VDEV_NEW2
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver,replace $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL $VDEV_B2
log_mustnot vdevs_in_pool $TESTPOOL $VDEV_C2
log_must zpool export $TESTPOOL
log_must zpool import -d $VDEV_DIR $TESTPOOL
log_must zpool offline $TESTPOOL $VDEV_B1
log_must zpool offline $TESTPOOL $VDEV_C1
check_split_pool
destroy_pool $TESTPOOL
rm -rf $VDEV_DIR

log_note "Without checksums, attach a disk to one that can't be read"
setup_vdevs
SPLIT_POOL_OPTS="-O checksum=off"
SPLIT_POOL_VDEVS="special $VDEV_SPECIAL"
create_split_pool $VDEV_A $VDEV_B1
SPLIT_POOL_OPTS=""
SPLIT_POOL_VDEVS=""
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach $TESTPOOL $VDEV_B1 $VDEV_B2
log_must zinject -d $VDEV_B1 -e io -T read $TESTPOOL
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL
log_must zinject -c all
log_must zpool status -v $TESTPOOL
# The resilver's reads of the old disk must have failed.
log_must test "$(get_vdev_prop read_errors $TESTPOOL $VDEV_B1)" -gt 0
log_must zpool clear $TESTPOOL
log_must zpool detach $TESTPOOL $VDEV_B2
log_must zpool export $TESTPOOL
log_must zpool import -d $VDEV_DIR $TESTPOOL
check_split_pool

log_pass "A resilver gives new devices the split blocks of a removed vdev"
