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
# For each child of the destination mirror in turn:
# 1. Create a pool on a two-way mirror, write 128k blocks and sync them
#    to that mirror.
# 2. Add a second two-way mirror, set REMOVE_MAX_SEGMENT to 8k and
#    remove the first mirror.
# 3. Verify with zdb that every block is split into more than 12
#    segments: more unique combinations than
#    zfs_reconstruct_indirect_combinations_max.
# 4. Export the pool and overwrite the data area of the child.
# 5. Import and scrub the pool.
# 6. Verify that no data error is reported, that only the damaged child
#    has checksum errors and that the file contents are intact.
#

verify_runnable "global"

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

#
# Print the smallest number of segments of the removed vdev 0 that an L0
# block of the file spans, or nothing when zdb prints no segment or block.
#
function min_segments # pool dataset file
{
	typeset obj

	obj=$(get_objnum $3)

	# Mapping entries: <0:offset:size> -> <vdev:offset:size>
	zdb -mmmm $1 0 | awk -F'[<:>]' '/^\t<0:/ { print $3, $4 }' \
	    > $DISKDIR/segments
	# Blocks: vdev:offset:size, hexadecimal like the entries
	zdb -ddddd $2 $obj | awk '$2 == "L0" { gsub(":", " ", $3); print $3 }' \
	    > $DISKDIR/blocks

	awk '
	function hex(s,    i, n) {
		n = 0
		for (i = 1; i <= length(s); i++)
			n = n * 16 + index("0123456789abcdef",
			    substr(s, i, 1)) - 1
		return (n)
	}
	NR == FNR {
		start[NR] = hex($1)
		end[NR] = start[NR] + hex($2)
		n = NR
		next
	}
	{
		c = 0
		if ($1 == 0) {
			s = hex($2)
			e = s + hex($3)
			for (i = 1; i <= n; i++)
				if (start[i] < e && end[i] > s)
					c++
		}
		if (min == "" || c < min)
			min = c
	}
	END { print min }' $DISKDIR/segments $DISKDIR/blocks
}

typeset digest errors segments

log_must save_tunable REMOVE_MAX_SEGMENT
log_onexit cleanup

for damaged in $DISK2 $DISK3; do
	log_must truncate -s $MINVDEVSIZE $DISK0 $DISK1 $DISK2 $DISK3

	log_must zpool create -f -o ashift=9 $TESTPOOL mirror $DISK0 $DISK1
	log_must zfs create -o compression=off -o recordsize=128k \
	    -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=128k count=16
	sync_pool $TESTPOOL
	digest=$(xxh128digest $TESTDIR/$TESTFILE0)

	log_must zpool add $TESTPOOL mirror $DISK2 $DISK3
	log_must set_tunable32 REMOVE_MAX_SEGMENT 8192
	log_must zpool remove $TESTPOOL mirror-0
	log_must wait_for_removal $TESTPOOL
	sync_pool $TESTPOOL

	segments=$(min_segments $TESTPOOL $TESTPOOL/$TESTFS \
	    $TESTDIR/$TESTFILE0)
	log_note "Every block is split into at least $segments segments"
	log_must test "$segments" -gt 12

	# Keep the labels and the boot area at the start, and the two
	# labels at the end of the device.
	log_must zpool export $TESTPOOL
	log_must dd if=/dev/urandom of=$damaged bs=1M seek=4 conv=notrunc \
	    count=$((MINVDEVSIZE / 1024 / 1024 - 5))

	log_must zpool import -d $DISKDIR $TESTPOOL
	log_must zpool scrub -w $TESTPOOL
	log_must zpool status -vpP $TESTPOOL

	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	for disk in $DISK2 $DISK3; do
		errors=$(get_vdev_prop checksum_errors $TESTPOOL $disk)
		if [[ $disk == "$damaged" ]]; then
			log_must test "$errors" -gt 0
		else
			log_must test "$errors" -eq 0
		fi
	done

	log_must zpool export $TESTPOOL
	log_must zpool import -d $DISKDIR $TESTPOOL
	log_must test "$(xxh128digest $TESTDIR/$TESTFILE0)" = "$digest"

	log_must zpool destroy $TESTPOOL
	log_must rm -f $DISK0 $DISK1 $DISK2 $DISK3
done

log_pass "Split blocks are reconstructed from the undamaged child."
