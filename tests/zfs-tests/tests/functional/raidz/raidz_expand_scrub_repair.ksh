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
#	A scrub that repairs damage while a raidz expansion is copying data
#	must neither lose those repairs nor report data errors.
#
# STRATEGY:
#	1. Create a raidz3 pool, fill it and attach a new disk with the
#	   reflow paused after a tenth of the data.
#	2. Three times: export the pool, zero the data area of three disks (the
#	   most raidz3 can repair), import it, start a scrub and then move
#	   the reflow pause point further on.  The reflow copies the damaged
#	   rows while the scrub repairs them.  Verify the scrub found no data
#	   errors and that the reflow is paused, not done.  At least one of
#	   the scrubs must have overlapped with the reflow (it checks its
#	   pause point once a second, so a short scrub can finish first).
#	3. Let the expansion finish, scrub again and verify that it repairs
#	   nothing: every repair of the earlier scrubs must have stuck.
#

typeset -r devs=6
typeset -r dev_size_mb=128
typeset -r nparity=3

typeset -a disks

function reflowed # pool
{
	zpool status -jp $1 | jq -r '.pools[].raidz_expand_stats.reflowed'
}

prefetch_disable=$(get_tunable PREFETCH_DISABLE)
scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done

	log_must set_tunable32 PREFETCH_DISABLE $prefetch_disable
	log_must set_tunable32 SCRUB_AFTER_EXPAND $scrub_after_expand
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

log_onexit cleanup

log_must set_tunable32 PREFETCH_DISABLE 1
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

for i in {0..$(($devs - 1))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done
log_must truncate -s 512M $TEST_BASE_DIR/dev-$devs

log_must zpool create -f -o cachefile=none $TESTPOOL raidz$nparity ${disks[@]}
log_must zfs set primarycache=metadata $TESTPOOL

log_must zfs create $TESTPOOL/fs
log_must fill_fs /$TESTPOOL/fs 1 512 102400 1 R
log_must zfs create -o compress=on $TESTPOOL/fs2
log_must fill_fs /$TESTPOOL/fs2 1 512 102400 1 R
log_must zfs create -o compress=on -o recordsize=8k $TESTPOOL/fs3
log_must fill_fs /$TESTPOOL/fs3 1 512 102400 1 R

# Pause the reflow after a tenth of the data, so most of it is still to be
# copied while the damaged pool is scrubbed.
log_must zpool sync $TESTPOOL
allocated=$(get_pool_prop allocated $TESTPOOL)
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((allocated / 10))
log_must zpool attach $TESTPOOL raidz$nparity-0 $TEST_BASE_DIR/dev-$devs
wait_raidz_expand_paused $TESTPOOL

typeset -i overlapped=0
for pause in 3 5 7; do
	log_must zpool export $TESTPOOL

	# Zero the data area (past the labels and boot area) of nparity disks.
	for (( i=0; i<$nparity; i=i+1 )); do
		log_must dd conv=notrunc if=/dev/zero of=$TEST_BASE_DIR/dev-$i \
		    bs=1M seek=4 count=$(($dev_size_mb-4))
	done

	# Import with the reflow still paused, start the scrub and only then
	# let the reflow copy another 20% of the data, so that it runs while
	# the scrub repairs.
	log_must zpool import -o cachefile=none -d $TEST_BASE_DIR $TESTPOOL
	before=$(reflowed $TESTPOOL)
	log_must zpool scrub $TESTPOOL
	target=$((allocated * pause / 10))
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $target
	log_must zpool wait -t scrub $TESTPOOL
	after=$(reflowed $TESTPOOL)
	log_note "reflowed $before before the scrub, $after after it"
	#
	# The reflow checks its pause point once a second, so a short scrub
	# can finish before it resumes (seen on FreeBSD).  Such a round does
	# not test the race, but its checks below still apply.
	#
	if (( after > before )); then
		(( overlapped += 1 ))
	else
		log_note "the reflow did not run during this scrub"
	fi
	log_must eval "zpool status -v $TESTPOOL"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must eval "zpool status $TESTPOOL | \
	    grep -q 'expansion of .* in progress'"
	log_must zpool clear $TESTPOOL
done

(( overlapped > 0 )) || \
    log_unsupported "the reflow did not run during any of the scrubs"

log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status -v $TESTPOOL"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"

log_pass "Scrub repairs survive a concurrent raidz expansion."
