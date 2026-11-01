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
# DESCRIPTION:
#	Verify that scrub and resilver still cover a dedup block whose DDT
#	entry was pruned, once the same data has been written again into a
#	new entry with several references.  That entry has the key of the
#	pruned block but other DVAs, and it is in the duplicate class that
#	the DDT walk of a scrub or resilver covers.
#
# STRATEGY:
#	1. Create a mirror of two file vdevs and a dataset with dedup=on.
#	2. Write a file, so that each of its blocks gets a unique entry,
#	   and prune those entries.
#	3. Write the same data twice, so that new entries with two
#	   references and the same keys exist at other DVAs.
#	4. Corrupt the file's blocks on one side of the mirror, and scrub:
#	   both sides must hold the same data again.
#	5. Replace the other side, and check that the new device received
#	   the file's blocks.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

VDEV_DIR=$TEST_BASE_DIR/dedup_prune_scrub
VDEV_A=$VDEV_DIR/a
VDEV_B=$VDEV_DIR/b
VDEV_C=$VDEV_DIR/c
# Offset of the allocatable space of a vdev, after the front labels
LABEL_START=$((4 * 1024 * 1024))

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	rm -rf $VDEV_DIR
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

function ddt_entries
{
	zpool status -D $TESTPOOL | awk '/dedup: DDT entries/ { print $4 }'
}

# Print "offset asize" in decimal for each L0 block of the file
function l0_dvas # dataset file
{
	typeset obj=$(get_objnum $2)

	zdb -ddddd $1 $obj | awk '
	    $2 == "L0" && $3 ~ /^0:[0-9a-f]+:[0-9a-f]+$/ {
		split($3, f, ":")
		print f[2], f[3]
	    }' | while read -r off asize; do
		echo $((16#$off)) $((16#$asize))
	done
}

# Whether both devices hold the same bytes at each block: same_blocks dev dev
function same_blocks
{
	typeset off asize

	while read -r off asize; do
		cmp -s \
		    <(dd if=$1 bs=512 skip=$(((off + LABEL_START) / 512)) \
		    count=$((asize / 512)) 2>/dev/null) \
		    <(dd if=$2 bs=512 skip=$(((off + LABEL_START) / 512)) \
		    count=$((asize / 512)) 2>/dev/null) || return 1
	done < $VDEV_DIR/dvas
	return 0
}

log_assert "Scrub and resilver cover dedup blocks whose DDT entry was pruned"
log_onexit cleanup

# Flush the DDT log every txg, so that entries reach the ZAP at once.
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000

log_must mkdir -p $VDEV_DIR
log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B $VDEV_C
log_must zpool create -f $TESTPOOL mirror $VDEV_A $VDEV_B
log_must zfs create -o dedup=sha256 -o recordsize=128k -o compression=off \
    $TESTPOOL/$TESTFS
typeset mnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# 2. Unique entries, pruned.
log_must dd if=/dev/urandom of=$mnt/file1 bs=128k count=8
sync_pool $TESTPOOL
log_must test "$(ddt_entries)" -eq 8
sleep 1
log_must zpool ddtprune -p 100 $TESTPOOL
sync_pool $TESTPOOL
typeset entries=$(ddt_entries)
[[ -z "$entries" || "$entries" -eq 0 ]] || \
    log_fail "DDT entries not pruned: $entries remain"

# 3. The same keys, with two references each, at other DVAs.
log_must dd if=$mnt/file1 of=$mnt/file2 bs=128k
log_must dd if=$mnt/file1 of=$mnt/file3 bs=128k
sync_pool $TESTPOOL
log_must test "$(ddt_entries)" -eq 8
log_must eval "l0_dvas $TESTPOOL/$TESTFS $mnt/file1 > $VDEV_DIR/dvas"
log_must test "$(wc -l < $VDEV_DIR/dvas)" -eq 8
typeset dva1=$(head -1 $VDEV_DIR/dvas)
typeset dva2=$(l0_dvas $TESTPOOL/$TESTFS $mnt/file2 | head -1)
log_note "file1 block 0 at $dva1, file2 block 0 at $dva2"
log_must test "$dva1" != "$dva2"

# 4. Corrupt file1's blocks on vdev b only, then scrub.
log_must zpool export $TESTPOOL
while read -r off asize; do
	log_must dd if=/dev/urandom of=$VDEV_B bs=512 \
	    seek=$(((off + LABEL_START) / 512)) count=$((asize / 512)) \
	    conv=notrunc
done < $VDEV_DIR/dvas
log_mustnot same_blocks $VDEV_A $VDEV_B
log_must zpool import -d $VDEV_DIR $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must zpool export $TESTPOOL
log_must same_blocks $VDEV_A $VDEV_B
log_must zpool import -d $VDEV_DIR $TESTPOOL

# 5. Resilver onto a new device.
log_must zpool replace -w $TESTPOOL $VDEV_B $VDEV_C
log_must zpool export $TESTPOOL
log_must same_blocks $VDEV_A $VDEV_C
log_must zpool import -d $VDEV_DIR $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_pass "Scrub and resilver cover dedup blocks whose DDT entry was pruned"
