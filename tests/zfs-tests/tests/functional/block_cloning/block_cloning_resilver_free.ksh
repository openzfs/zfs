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
. $STF_SUITE/include/kstat.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	Freeing one reference to a cloned block while a resilver has queued
#	the block must not drop the queued read. The block still has another
#	reference, and the resilver must copy it.
#
# STRATEGY:
#	1. Write a large file, then a one-block file and a clone of it.
#	2. Delay reads of the source, and attach a new device. The sorted
#	   resilver issues the large file's extents first, so the shared
#	   block stays queued after traversal has finished.
#	3. Remove the clone while its block is queued.
#	4. Detach the source, and verify the remaining file cold.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	rm -rf $workdir
}

# The number of scans of the pool whose traversal has finished.
function traversals
{
	kstat dbgmsg | grep -c "scan complete for $TESTPOOL1 "
}

log_assert "A queued resilver read survives a freed clone of its block"
workdir=$(mktemp -d $TEST_BASE_DIR/block_cloning_resilver_free.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must truncate -s 512M $workdir/disk-0 $workdir/disk-1
log_must zpool create -o feature@block_cloning=enabled $TESTPOOL1 \
    $workdir/disk-0
log_must zfs create -o compression=off -o recordsize=128k $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/filler bs=1M count=64
sync_pool $TESTPOOL1
log_must dd if=/dev/urandom of=$workdir/expected bs=128k count=1
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must clonefile -f $mntpnt/file $mntpnt/clone
sync_pool $TESTPOOL1

log_must zinject -d $workdir/disk-0 -D 200:1 -T read $TESTPOOL1
typeset -i i before=$(traversals)
log_must zpool attach $TESTPOOL1 $workdir/disk-0 $workdir/disk-1
for (( i = 0; i < 300; i++ )); do
	(( $(traversals) > before )) && break
	sleep 0.1
done
(( i < 300 )) || log_fail "resilver traversal did not finish"
log_must is_pool_resilvering $TESTPOOL1
log_must rm $mntpnt/clone
sync_pool $TESTPOOL1
log_must zinject -c all
log_must zpool wait -t resilver $TESTPOOL1

log_must zpool detach $TESTPOOL1 $workdir/disk-0
log_must zpool export $TESTPOOL1
log_must zpool import -d $workdir $TESTPOOL1
log_must cmp $workdir/expected $mntpnt/file

log_pass "A queued resilver read survived a freed clone of its block"
