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

. "$STF_SUITE"/include/libtest.shlib

#
# DESCRIPTION:
#	Blocks written before a RAID-Z expansion keep their old width.
#	'zfs rewrite' gives them the width of the expanded vdev once the
#	expansion has completed, but not while it is still running, and
#	leaves deduplicated blocks where they are.
#
# STRATEGY:
#	1. Create a 5-wide RAID-Z2 pool with ashift=12 and no compression.
#	   Write a 128K, a 16K and a 4K block, two copies of a deduplicated
#	   128K block, and some data to keep the expansion running.
#	2. Attach a sixth device and pause the expansion.
#	3. Rewrite the three blocks. They must move, but not change their
#	   allocated size.
#	4. Let the expansion complete. The blocks must be unchanged.
#	5. Rewrite the three blocks again. They must now be allocated at the
#	   new width, except for the 4K block, which is the same at both.
#	6. Rewrite the deduplicated blocks. They must stay where they are.
#	7. Scrub the pool and verify the data.
#

verify_runnable "global"

typeset -r mnt="$TEST_BASE_DIR/raidz-rewrite-mnt"
typeset -r blocks="128k 16k 4k"
typeset -r files="128k/blk 16k/blk 4k/blk dedup/a dedup/b"
typeset -a disks
typeset -A bp sum

function cleanup
{
	poolexists "$TESTPOOL" && destroy_pool "$TESTPOOL"
	log_must rm -f "${disks[@]}"
	log_must rm -rf "$mnt"
	log_must restore_tunable RAIDZ_EXPAND_MAX_REFLOW_BYTES
	log_must restore_tunable SCRUB_AFTER_EXPAND
}

# Print the DVA and the births of the only L0 block of file $2 in dataset $1.
function get_bp # <dataset> <file>
{
	zdb -Ovv "$TESTPOOL/$1" "$2" |
	    awk '/ L0 / { n++; bp = $3 " " $6 } END { if (n == 1) print bp }'
}

# Require the blocks to be allocated the given sizes (in hex), and remember
# where they are. Each block is the file blk of the dataset named after its
# size.
function check_asizes # <128K asize> <16K asize> <4K asize>
{
	typeset -A want=([128k]="$1" [16k]="$2" [4k]="$3")
	typeset size dva

	for size in $blocks; do
		bp[$size]=$(get_bp "$size" blk)
		dva=${bp[$size]%% *}
		log_note "$size: ${bp[$size]}"
		[[ "${dva##*:}" == "${want[$size]}" ]] ||
		    log_fail "$size: not allocated 0x${want[$size]}"
	done
}

# Rewrite the blocks and require them to move to the given sizes.
function rewrite_blocks # <128K asize> <16K asize> <4K asize>
{
	typeset -A old
	typeset size

	for size in $blocks; do
		old[$size]=${bp[$size]}
	done
	log_must zfs rewrite "$mnt/128k/blk" "$mnt/16k/blk" "$mnt/4k/blk"
	sync_pool "$TESTPOOL"
	check_asizes "$@"
	for size in $blocks; do
		[[ "${bp[$size]%% *}" != "${old[$size]%% *}" ]] ||
		    log_fail "$size: not rewritten"
	done
}

# Rewrite the deduplicated blocks and require them to stay where they are,
# with a new logical birth time.
function rewrite_dedup
{
	typeset -A old
	typeset file new

	for file in a b; do
		old[$file]=$(get_bp dedup $file)
	done
	log_must zfs rewrite "$mnt/dedup/a" "$mnt/dedup/b"
	sync_pool "$TESTPOOL"
	for file in a b; do
		new=$(get_bp dedup $file)
		log_note "dedup/$file: ${old[$file]} -> $new"
		[[ "${new%% *}" == "${old[$file]%% *}" ]] ||
		    log_fail "dedup/$file: moved"
		[[ "${new##* }" != "${old[$file]##* }" ]] ||
		    log_fail "dedup/$file: not rewritten"
	done
}

log_assert "zfs rewrite applies the width of an expanded RAID-Z vdev"
log_onexit cleanup
log_must save_tunable RAIDZ_EXPAND_MAX_REFLOW_BYTES
log_must save_tunable SCRUB_AFTER_EXPAND
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

for i in 0 1 2 3 4 5; do
	disks[i]="$TEST_BASE_DIR/raidz-rewrite-dev-$i"
	log_must truncate -s 256M "${disks[i]}"
done

# The data is read back from the devices and not from the ARC.
log_must zpool create -f -o ashift=12 -O compression=off \
    -O primarycache=metadata -O mountpoint="$mnt" "$TESTPOOL" raidz2 \
    "${disks[0]}" "${disks[1]}" "${disks[2]}" "${disks[3]}" "${disks[4]}"

for size in $blocks; do
	log_must zfs create -o recordsize="$size" "$TESTPOOL/$size"
	log_must dd if=/dev/urandom of="$mnt/$size/blk" bs="$size" count=1
done
log_must zfs create -o recordsize=128k -o dedup=on "$TESTPOOL/dedup"
log_must dd if=/dev/urandom of="$mnt/dedup/a" bs=128k count=1
log_must dd if="$mnt/dedup/a" of="$mnt/dedup/b" bs=128k count=1
log_must dd if=/dev/zero of="$mnt/filler" bs=1M count=16
for file in $files; do
	sum[$file]=$(xxh128digest "$mnt/$file")
done
sync_pool "$TESTPOOL"

# 5 wide: 3 data and 2 parity. 6 wide: 4 data and 2 parity.
check_asizes 36000 9000 3000

log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 1
log_must zpool attach "$TESTPOOL" raidz2-0 "${disks[5]}"
wait_raidz_expand_paused "$TESTPOOL"
log_must eval "zpool status $TESTPOOL | grep -q 'expansion of .* in progress'"

rewrite_blocks 36000 9000 3000

log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand "$TESTPOOL"
# Blocks born from a few txgs after the expansion completes use the new width.
sync_pool "$TESTPOOL"
sync_pool "$TESTPOOL"
sync_pool "$TESTPOOL"
check_asizes 36000 9000 3000

rewrite_blocks 30000 6000 3000
rewrite_dedup

log_must zpool scrub -w "$TESTPOOL"
log_must check_pool_status "$TESTPOOL" "scan" "repaired 0B"
log_must check_pool_status "$TESTPOOL" "errors" "No known data errors"
for file in $files; do
	log_must [ "${sum[$file]}" = "$(xxh128digest "$mnt/$file")" ]
done

log_pass "zfs rewrite applies the width of an expanded RAID-Z vdev"
