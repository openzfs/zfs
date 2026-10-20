#! /bin/ksh -p
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
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#
# A block that device removal split into many segments, whose copies on
# one mirror child are all damaged, must be reconstructed from the other
# child by a scrub.  Only the damaged child may be charged checksum
# errors.
#
# STRATEGY:
#
# 1. Create a pool on a two-way mirror and write 128k blocks.
# 2. Add a second two-way mirror, set REMOVE_MAX_SEGMENT to 8k and
#    remove the first mirror, so each block is split into at least 16
#    segments: more unique combinations than
#    zfs_reconstruct_indirect_combinations_max.
# 3. Export the pool and overwrite the data area of one child of the
#    remaining mirror.
# 4. Import and scrub the pool.
# 5. Verify that no data error is reported, that only the damaged child
#    has checksum errors and that the file contents are intact.
#

DISKDIR=$(mktemp -d)
DISK0=$DISKDIR/dsk0
DISK1=$DISKDIR/dsk1
DISK2=$DISKDIR/dsk2
DISK3=$DISKDIR/dsk3

function cleanup
{
	log_must restore_tunable REMOVE_MAX_SEGMENT
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

function cksum_errors # pool vdev
{
	zpool status -pP $1 | awk -v vdev=$2 '$1 == vdev { print $5 }'
}

log_must truncate -s $MINVDEVSIZE $DISK0 $DISK1 $DISK2 $DISK3

log_must save_tunable REMOVE_MAX_SEGMENT
log_onexit cleanup

log_must zpool create -f -o ashift=9 $TESTPOOL mirror $DISK0 $DISK1
log_must zfs create -o compression=off -o recordsize=128k \
    -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=128k count=16
typeset digest=$(xxh128digest $TESTDIR/$TESTFILE0)

log_must zpool add $TESTPOOL mirror $DISK2 $DISK3
log_must set_tunable32 REMOVE_MAX_SEGMENT 8192
log_must zpool remove $TESTPOOL mirror-0
log_must wait_for_removal $TESTPOOL

# Keep the labels and the boot area at the start, and the two labels
# at the end of the device.
log_must zpool export $TESTPOOL
log_must dd if=/dev/urandom of=$DISK3 bs=1M seek=4 conv=notrunc \
    count=$((MINVDEVSIZE / 1024 / 1024 - 5))

log_must zpool import -d $DISKDIR $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must zpool status -vpP $TESTPOOL

log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must test "$(cksum_errors $TESTPOOL $DISK2)" -eq 0
log_must test "$(cksum_errors $TESTPOOL $DISK3)" -gt 0

log_must zpool export $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL
log_must test "$(xxh128digest $TESTDIR/$TESTFILE0)" = "$digest"

log_pass "Split blocks are reconstructed from the undamaged child."
