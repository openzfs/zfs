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
#	During a raidz expansion, a block that straddles the reflow offset
#	has rows at their new location and rows still at their old one.
#	Damage from a single failed disk must remain repairable in such a
#	block, also when the reflow has copied some of the damage to the
#	new location.
#
# STRATEGY:
#	1. Create a raidz1 pool, write a file and attach a new disk with the
#	   reflow paused early on.
#	2. Export the pool and zero the part of one disk that holds the data
#	   the reflow has not copied yet.
#	3. Import the pool and let the reflow copy some more of the data, in
#	   chunks that are not a multiple of the block size so that it can
#	   stop inside a block.  It copies the damage along, spread over all
#	   disks.
#	4. Verify that a block of the file straddles the new reflow offset,
#	   with its first part copied from the damaged area (if none does,
#	   move the pause point on a little and look again).  Every row of
#	   that block has one damaged sector, on the zeroed disk in the rows
#	   at the old location and wherever the reflow put it in the others.
#	5. Scrub with the reflow paused.  Verify that it found no data
#	   errors and that the file is intact.
#	6. Let the expansion finish, scrub again and verify that it repairs
#	   nothing.
#

typeset -r devs=6
typeset -r dev_size_mb=256
# A directory of our own, so that zdb -e and zpool import only see this pool.
typeset -r devdir=$TEST_BASE_DIR/raidz_expand_boundary_repair

typeset -a disks

prefetch_disable=$(get_tunable PREFETCH_DISABLE)
scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)
max_copy_bytes=$(get_tunable RAIDZ_EXPAND_MAX_COPY_BYTES)

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -rf "$devdir"

	log_must set_tunable32 PREFETCH_DISABLE $prefetch_disable
	log_must set_tunable32 SCRUB_AFTER_EXPAND $scrub_after_expand
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must set_tunable64 RAIDZ_EXPAND_MAX_COPY_BYTES $max_copy_bytes
}

function reflowed # pool
{
	zpool status -jp $1 | jq -r '.pools[].raidz_expand_stats.reflowed'
}

#
# The reflow offset (in the raidz vdev's address space) of the exported pool.
#
function reflow_offset
{
	zdb -e -p $devdir -u $TESTPOOL | \
	    awk '/raidz_reflow/ { sub("off=", "", $3); print $3 }'
}

#
# Print the offset and allocated size of each level 0 block of the file in
# the exported pool, in decimal.
#
function file_blocks # object
{
	zdb -e -p $devdir -ddddd $TESTPOOL/fs $1 | \
	    awk '$2 == "L0" { print $3 }' | while IFS=: read _ off asize; do
		echo $((16#$off)) $((16#$asize))
	done
}

log_onexit cleanup

log_must mkdir -p $devdir

log_must set_tunable32 PREFETCH_DISABLE 1
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

for i in {0..$(($devs - 1))}; do
	device=$devdir/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done
log_must truncate -s ${dev_size_mb}M $devdir/dev-$devs

log_must zpool create -f -o cachefile=none -o ashift=12 $TESTPOOL \
    raidz1 ${disks[@]}
log_must zfs create -o recordsize=128k -o compression=off \
    -o primarycache=metadata $TESTPOOL/fs
log_must dd if=/dev/urandom of=/$TESTPOOL/fs/file bs=1M count=400
typeset -r digest=$(xxh128digest /$TESTPOOL/fs/file)
typeset -r object=$(ls -i /$TESTPOOL/fs/file | awk '{print $1}')

log_must zpool sync $TESTPOOL
allocated=$(get_pool_prop allocated $TESTPOOL)
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((allocated / 80))
log_must zpool attach $TESTPOOL raidz1-0 $devdir/dev-$devs
wait_raidz_expand_paused $TESTPOOL
log_must zpool export $TESTPOOL

#
# Rows past the reflow offset are still at their old location, row r on
# each disk at byte offset 4M (the labels and boot area) + r * 4k.  Zero
# disk 0 from the first such row on.  The copied part of the pool is in the
# rows before that (they are wider) and stays intact.
#
offset1=$(reflow_offset)
log_note "reflow offset $offset1 after the first pause"
(( offset1 > 0 )) || log_fail "no reflow offset in the uberblock"
row=$((offset1 / 4096 / devs))
log_must dd conv=notrunc if=/dev/zero of=$devdir/dev-0 bs=4k \
    seek=$((1024 + row)) count=$((dev_size_mb * 256 - 1024 - row - 256))

log_must zpool import -o cachefile=none -d $devdir $TESTPOOL
log_must set_tunable64 RAIDZ_EXPAND_MAX_COPY_BYTES $((1024 * 1024 + 4096))

#
# The reflow can still stop between two blocks, e.g. at the end of a
# metaslab.  Move the pause point on until a block straddles the offset,
# with at least one full row on either side (a full row of the block spans
# every old disk; the last row can be shorter).  Each column of the block is
# on the same old disk in every row; the column on disk 0 must not be the
# parity (raidz1 swaps the first two columns in every other MiB), or the
# scrub finds the data intact and only rewrites the parity.
#
typeset -i straddling=0
rowsize=$((devs * 4096))
pause=$((allocated / 40))
for (( i = 0; i < 20 && straddling == 0; i++ )); do
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $pause
	wait_raidz_expand_paused $TESTPOOL
	pause=$(($(reflowed $TESTPOOL) + allocated / 80))
	log_must zpool export $TESTPOOL

	offset2=$(reflow_offset)
	log_note "reflow offset $offset2 after the second pause"
	file_blocks $object | while read off asize; do
		if (( off >= offset1 && off + rowsize <= offset2 &&
		    offset2 + 2 * rowsize <= off + asize &&
		    (off / 4096 + ((off >> 20) & 1)) % devs != 0 )); then
			log_note "block at $off (asize $asize) straddles it"
			(( straddling += 1 ))
		fi
	done
	log_must zpool import -o cachefile=none -d $devdir $TESTPOOL
done
(( straddling > 0 )) || \
    log_unsupported "no block of the file straddles the reflow offset"

before=$(reflowed $TESTPOOL)
log_must zpool scrub -w $TESTPOOL
log_note "reflowed $before before the scrub, $(reflowed $TESTPOOL) after it"
log_must eval "zpool status -v $TESTPOOL"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must eval "zpool status $TESTPOOL | grep -q 'expansion of .* in progress'"
log_mustnot check_pool_status $TESTPOOL "scan" "repaired 0B"
[[ $(xxh128digest /$TESTPOOL/fs/file) == $digest ]] || \
    log_fail "the file changed"
log_must zpool clear $TESTPOOL

log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status -v $TESTPOOL"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
[[ $(xxh128digest /$TESTPOOL/fs/file) == $digest ]] || \
    log_fail "the file changed"

log_pass "Damage in a block across the raidz reflow offset is repairable."
