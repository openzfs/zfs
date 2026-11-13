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
# A block that device removal split into segments must be reconstructed
# from, and repaired on, each copy below a hot spare: once resilvered, the
# spare and the device it replaced both hold one.
#
# STRATEGY:
#
# 1. Create a pool on one disk with a hot spare and write 128k blocks.
# 2. Add a disk, or a two-way mirror, set REMOVE_MAX_SEGMENT to 32k and
#    remove the first disk, so each block is split into segments.
# 3. Replace the added disk, or the second child of the mirror, with the
#    spare.
# 4. Export the pool and overwrite the data area of the added disk, of
#    the spare, or of both the spare and the other child of the mirror.
# 5. Import and scrub the pool.
# 6. Verify that no data error is reported and that only the damaged disks
#    have checksum errors.
# 7. Offline every other disk, so that only the damaged ones can be read.
# 8. Verify that the file contents are intact and that a scrub finds no
#    error.
#

DISKDIR=$(mktemp -d)
DISK0=$DISKDIR/dsk0
DISK1=$DISKDIR/dsk1
DISK2=$DISKDIR/dsk2
SPARE=$DISKDIR/spare

function cleanup
{
	log_must restore_tunable REMOVE_MAX_SEGMENT
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

function cksum_errors # pool vdev
{
	zpool status -pP $1 | awk -v vdev=$2 '$1 == vdev { print $5; exit }'
}

#
# Build the pool with a single added disk ("disk") or a two-way mirror
# ("mirror"), replace $replaced with the spare, damage the disks in
# $damaged, and check that a scrub recovers every block and repairs them.
#
function test_spare # disk|mirror replaced damaged
{
	typeset layout=$1
	typeset replaced=$2
	typeset damaged=$3
	typeset added=$DISK1
	typeset others=""

	[[ $layout == "mirror" ]] && added="mirror $DISK1 $DISK2"
	for disk in $added $SPARE; do
		[[ $disk != "mirror" && " $damaged " != *" $disk "* ]] && \
		    others="$others $disk"
	done

	log_must truncate -s $MINVDEVSIZE $DISK0 $DISK1 $DISK2 $SPARE
	log_must zpool create -f -o ashift=9 $TESTPOOL $DISK0 spare $SPARE
	log_must zfs create -o compression=off -o recordsize=128k \
	    -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=128k count=16
	typeset digest=$(xxh128digest $TESTDIR/$TESTFILE0)

	log_must zpool add -f $TESTPOOL $added
	log_must zpool remove $TESTPOOL $DISK0
	log_must wait_for_removal $TESTPOOL

	log_must zpool replace -w $TESTPOOL $replaced $SPARE

	# Keep the labels and the boot area at the start, and the two labels
	# at the end of the device.
	log_must zpool export $TESTPOOL
	for disk in $damaged; do
		log_must dd if=/dev/urandom of=$disk bs=1M seek=4 conv=notrunc \
		    count=$((MINVDEVSIZE / 1024 / 1024 - 5))
	done

	log_must zpool import -d $DISKDIR $TESTPOOL
	log_must zpool scrub -w $TESTPOOL
	log_must zpool status -vpP $TESTPOOL

	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	for disk in $damaged; do
		log_must test "$(cksum_errors $TESTPOOL $disk)" -gt 0
	done
	for disk in $others; do
		log_must test "$(cksum_errors $TESTPOOL $disk)" -eq 0
	done

	# Only the damaged disks are left to read from: the scrub repaired them.
	log_must zpool clear $TESTPOOL
	for disk in $others; do
		log_must zpool offline $TESTPOOL $disk
	done
	log_must zpool export $TESTPOOL
	log_must zpool import -d $DISKDIR $TESTPOOL
	log_must test "$(xxh128digest $TESTDIR/$TESTFILE0)" = "$digest"
	log_must zpool scrub -w $TESTPOOL
	log_must zpool status -vpP $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	for disk in $damaged; do
		log_must test "$(cksum_errors $TESTPOOL $disk)" -eq 0
	done
	log_must zpool destroy $TESTPOOL
	log_must rm -f $DISK0 $DISK1 $DISK2 $SPARE
}

log_must save_tunable REMOVE_MAX_SEGMENT
log_onexit cleanup
log_must set_tunable32 REMOVE_MAX_SEGMENT 32768

# The spare and the disk it replaced form a top-level vdev.
test_spare disk $DISK1 $DISK1

# The spare replaced a child of a mirror.
test_spare mirror $DISK2 $SPARE

# The spare and the other child of the mirror are both damaged: only the
# disk the spare replaced holds the data.
test_spare mirror $DISK2 "$DISK1 $SPARE"

log_pass "Split blocks are reconstructed from, and repaired on, each" \
    "copy below a hot spare."
