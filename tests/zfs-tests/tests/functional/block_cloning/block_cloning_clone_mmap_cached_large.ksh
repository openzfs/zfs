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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	A clone of 2 GiB or more in one chunk updates the destination's
#	cached pages.
#
# STRATEGY:
#	1. Create a 4 MiB destination file in a dataset with 4 MiB records
#	   and fault its pages into the page cache through a mapping.
#	2. Create a sparse source file of 513 records (just over 2 GiB),
#	   with data in its first record only.
#	3. Clone the whole source over the destination: with 4 MiB records
#	   that is a single chunk of more than 2 GiB.
#	4. Read the destination's first record back, which reads through the
#	   cached pages: it must match the source.
#

verify_runnable "global"

claim="A clone of more than 2 GiB updates the destination's cached pages"

FS=$TESTPOOL/$TESTFS
MNT=/$FS
RECSZ=$((4 * 1024 * 1024))
SIZE=$((513 * RECSZ))

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $TEST_BASE_DIR/src0.$$ $TEST_BASE_DIR/dst0.$$
}

log_onexit cleanup

log_assert $claim

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS
log_must zfs create -o recordsize=4M -o compression=off $FS

log_must truncate -s $SIZE $MNT/src
log_must dd if=/dev/urandom of=$MNT/src bs=4M count=1 conv=notrunc
log_must dd if=/dev/urandom of=$MNT/dst bs=4M count=1
log_must sync_pool $TESTPOOL

log_must mmap_stale_check touch $MNT/dst
log_must clonefile -f $MNT/src $MNT/dst

log_must dd if=$MNT/src of=$TEST_BASE_DIR/src0.$$ bs=4M count=1
log_must dd if=$MNT/dst of=$TEST_BASE_DIR/dst0.$$ bs=4M count=1
log_must cmp $TEST_BASE_DIR/src0.$$ $TEST_BASE_DIR/dst0.$$

# The record was cloned, not copied.
log_must sync_pool $TESTPOOL
log_must [ "$(get_same_blocks $FS src $FS dst)" = "0" ]

log_pass $claim
