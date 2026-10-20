#!/bin/ksh
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
# Description:
# zdb -S must not present a partial simulated DDT histogram as complete.
# If the pool traversal stops on a block it can't read, zdb must say so
# and exit non-zero.
#
# Strategy:
# 1. Create a pool on a file with a dataset that keeps one copy of its
#    metadata (redundant_metadata=none), and write three files, two of
#    them identical.
# 2. zdb -S must succeed and show both the unique and the duplicated
#    blocks.
# 3. Export the pool and overwrite the L1 indirect block of one file.
# 4. zdb -e -S must fail and report that the histogram is incomplete.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV $TEST_BASE_DIR/zdb_sddt.out $TEST_BASE_DIR/zdb_sddt.err
	rm -rf $VDEVDIR
}

log_assert "zdb -S reports an incomplete traversal instead of a short histogram"
log_onexit cleanup

VDEVDIR=$TEST_BASE_DIR/zdb_sddt
VDEV=$VDEVDIR/vdev
OUT=$TEST_BASE_DIR/zdb_sddt.out
ERR=$TEST_BASE_DIR/zdb_sddt.err

log_must mkdir -p $VDEVDIR
log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -O compression=off $TESTPOOL1 $VDEV
log_must zfs create -o redundant_metadata=none -o recordsize=128k \
    $TESTPOOL1/fs
mntpnt=$(get_prop mountpoint $TESTPOOL1/fs)

log_must dd if=/dev/urandom of=$mntpnt/a bs=1M count=8
log_must dd if=/dev/urandom of=$mntpnt/b bs=1M count=8
log_must cp $mntpnt/b $mntpnt/b2
sync_pool $TESTPOOL1 true

# The healthy pool: 64 unique blocks (refcnt 1), 64 duplicated (refcnt 2).
log_must eval "zdb -S $TESTPOOL1 >$OUT 2>$ERR"
cat $OUT
log_must eval "awk '\$1 == \"1\" && \$2 == \"64\"' $OUT | grep -q ."
log_must eval "awk '\$1 == \"2\" && \$2 == \"64\"' $OUT | grep -q ."

# Find the L1 indirect block of file a: "0 L1 vdev:offset:asize ..."
obj=$(get_objnum $mntpnt/a)
dva=$(zdb -ddddd $TESTPOOL1/fs $obj | awk '$2 == "L1" {print $3; exit}')
log_note "L1 of object $obj: $dva"
[[ -n "$dva" ]] || log_fail "no L1 block found for object $obj"
offset=$(echo $dva | cut -d: -f2)
asize=$(echo $dva | cut -d: -f3)

log_must zpool export $TESTPOOL1

# Overwrite it (the vdev data starts after the 4 MiB of front labels).
log_must dd if=/dev/urandom of=$VDEV bs=512 conv=notrunc \
    seek=$(( (0x$offset + 0x400000) / 512 )) count=$(( 0x$asize / 512 ))

log_mustnot eval "zdb -e -p $VDEVDIR -S $TESTPOOL1 >$OUT 2>$ERR"
cat $OUT $ERR
log_must grep -q "incomplete" $ERR

log_must zpool import -d $VDEVDIR $TESTPOOL1

log_pass "zdb -S reports an incomplete traversal instead of a short histogram"
