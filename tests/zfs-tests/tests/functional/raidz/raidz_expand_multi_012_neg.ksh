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
#	While a raidz expansion that adds several disks at once is in
#	progress, the part of the vdev which has not been reflowed yet still
#	uses the layout from before the expansion, which is several disks
#	narrower, not one.  Replacing a raidz child with a device that is too
#	small for that layout must be refused, both before and after the
#	pool is re-imported with the expansion paused.  This is the
#	several-disk counterpart of raidz_expand_012_neg.
#
# STRATEGY:
#	For each parity (1, 2, 3):
#	1. Create a raidz pool and write some data to it
#	2. Verify that replacing a child with a too small device fails
#	3. Pause the reflow and attach three new devices to the raidz vdev
#	   in one expansion
#	4. Verify that replacing a child with the too small device fails
#	5. Export and import the pool, verify the replacement still fails
#	6. Replace a child with a device of the same size and wait for it
#	7. Let the expansion complete, scrub and verify the file checksum
#

verify_runnable "global"

typeset -r added=3
typeset -r dev_size_mb=256
#
# Each child of a raidz vdev expanded from W to W+3 children must provide
# at least 1/W of the vdev's allocatable space until the reflow completes.
# For 256M children and parity 1 to 3 (W = 3 to 5) this is more than
# 240M, while 1/(W+2), one disk narrower than the expanded vdev, is less
# than 180M.
#
typeset -r small_size_mb=224
typeset -r devs=$((3 + 2 + added + 1))
typeset -r devdir=$TEST_BASE_DIR/raidz_expand_multi_012_neg

embedded_slog_min_ms=$(get_tunable EMBEDDED_SLOG_MIN_MS)
original_scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -rf "$devdir"

	log_must set_tunable32 EMBEDDED_SLOG_MIN_MS $embedded_slog_min_ms
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must set_tunable32 SCRUB_AFTER_EXPAND $original_scrub_after_expand
}

log_onexit cleanup

log_must mkdir -p "$devdir"
log_must set_tunable32 EMBEDDED_SLOG_MIN_MS 99999
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

pool=$TESTPOOL
opts="-o cachefile=none"
small=$devdir/dev-small

for nparity in 1 2 3; do
	unset disks
	typeset -a disks
	typeset width=$((nparity + 2))
	typeset raid=raidz$nparity

	log_note "raidz$nparity, $width -> $((width + added)) wide"
	for i in {1..$devs}; do
		device=$devdir/dev-$i
		log_must rm -f $device
		log_must truncate -s ${dev_size_mb}M $device
		disks[${#disks[*]}+1]=$device
	done
	log_must rm -f $small
	log_must truncate -s ${small_size_mb}M $small

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must zpool create -f $opts $pool $raid ${disks[1..$width]}
	log_must zfs set compression=off $pool
	log_must dd if=/dev/urandom of=/$pool/file bs=1024k count=192
	log_must zpool sync $pool
	typeset digest=$(xxh128digest /$pool/file)

	# Without an expansion in progress the small device is refused.
	log_mustnot_expect "is too small" \
	    zpool replace -f $pool ${disks[1]} $small

	# Attach the new devices, pausing the reflow right at the start.
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 1
	log_must zpool attach -f $pool ${raid}-0 \
	    ${disks[$((width + 1))..$((width + added))]}
	wait_raidz_expand_paused $pool

	# The small device must be refused during the expansion.
	log_mustnot_expect "is too small" \
	    zpool replace -f $pool ${disks[1]} $small

	# Also when the pool is imported with the expansion in progress.
	log_must_busy zpool export $pool
	log_must zpool import $opts -d $devdir $pool
	wait_raidz_expand_paused $pool
	log_mustnot_expect "is too small" \
	    zpool replace -f $pool ${disks[1]} $small

	# A device of the original size is accepted.
	log_must zpool replace -f $pool ${disks[1]} ${disks[$devs]}
	log_must zpool wait -t replace $pool
	log_must check_pool_status $pool "scan" "with 0 errors"

	# Let the expansion complete and verify the pool.
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must zpool wait -t raidz_expand $pool

	log_must zpool scrub -w $pool
	log_must zpool status -v $pool
	log_must check_pool_status $pool "scan" "with 0 errors"
	log_must check_pool_status $pool "errors" "No known data errors"
	[[ $(xxh128digest /$pool/file) == $digest ]] || \
	    log_fail "the file changed"

	log_must_busy zpool destroy $pool
done

log_pass "raidz expansion by several disks refuses too small replacement" \
    "devices."
