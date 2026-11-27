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

#
# DESCRIPTION:
# A write that fails on one side of a mirror, and that the mirror absorbs,
# must be recorded in that side's DTL.  Otherwise 'zpool offline' accepts
# the only complete side, and a resilver does not repair the missed blocks.
#
# STRATEGY:
# 1. Create a mirror of two file vdevs.
# 2. Fail writes to the second vdev with 'zinject -F', which, like a real
#    device error, does not mark the failed I/O as retried.
# 3. Write a file and sync, then clear the injection.
# 4. Verify that the second vdev reported write errors and is ONLINE.
# 5. Verify that the first vdev cannot be offlined.
# 6. Resilver, offline the first vdev, re-import, and verify that the
#    second vdev alone holds a correct copy of the file.
#

verify_runnable "global"

VDEV1=$TEST_BASE_DIR/zpool_offline_we_1
VDEV2=$TEST_BASE_DIR/zpool_offline_we_2
FILE=/$TESTPOOL1/file

function cleanup
{
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	rm -f $VDEV1 $VDEV2
}

log_assert "A failed write that a mirror absorbs is recorded in the DTL"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2
log_must zpool create -f $TESTPOOL1 mirror $VDEV1 $VDEV2
# Keep ZED from faulting the vdev over its I/O errors, so that only the
# DTL decides whether the first vdev may be offlined.
log_must zpool set io_n=1000000 $TESTPOOL1 $VDEV2

log_must zinject -d $VDEV2 -e io -T write -F $TESTPOOL1
log_must dd if=/dev/urandom of=$FILE bs=1M count=8 conv=fsync
sync_pool $TESTPOOL1
log_must zinject -c all
typeset sum=$(xxh128digest $FILE)

typeset -i errs=$(zpool status -p $TESTPOOL1 | \
    awk -v v=$VDEV2 '$1 == v {print $4}')
(( errs > 0 )) || log_fail "no write errors on $VDEV2"
log_must check_state $TESTPOOL1 $VDEV2 "online"

# The second vdev missed the writes, so the first is the only full copy.
log_mustnot zpool offline $TESTPOOL1 $VDEV1

log_must zpool resilver $TESTPOOL1
# The resilver starts in the next txg sync; sync so that the wait sees it.
sync_pool $TESTPOOL1
log_must zpool wait -t resilver $TESTPOOL1
log_must is_pool_resilvered $TESTPOOL1
log_must zpool offline $TESTPOOL1 $VDEV1

# Read the file from the second vdev alone, not from the ARC.
log_must zpool export $TESTPOOL1
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1
log_must check_state $TESTPOOL1 $VDEV1 "offline"
[[ $(xxh128digest $FILE) == "$sum" ]] || \
    log_fail "$FILE differs when read from $VDEV2 alone"
log_must zpool scrub -w $TESTPOOL1
typeset -i cks=$(zpool status -p $TESTPOOL1 | \
    awk -v v=$VDEV2 '$1 == v {print $5}')
(( cks == 0 )) || log_fail "$cks checksum errors on $VDEV2"

log_pass "A failed write that a mirror absorbs is recorded in the DTL"
