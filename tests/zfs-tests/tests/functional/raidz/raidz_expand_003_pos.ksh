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
# Copyright (c) 2021 by vStack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_wait/zfs_wait.kshlib

#
# DESCRIPTION:
#	Check raidz expansion is able to work correctly under i/o load.
#
# STRATEGY:
#	1. Create block device files for the test raidz pool
#	2. For each parity value [1..3]
#	    - create raidz pool with minimum block device files required
#	    - create couple of datasets with different recordsize and fill it
#	    - set a max reflow value near pool capacity
#	    - wait for reflow to reach this max
#	    - verify pool
#	    - set reflow bytes to max value to complete the expansion

typeset -r devs=10
typeset -r dev_size_mb=256

typeset -a disks

embedded_slog_min_ms=$(get_tunable EMBEDDED_SLOG_MIN_MS)

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done

	log_must set_tunable32 EMBEDDED_SLOG_MIN_MS $embedded_slog_min_ms
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

function wait_expand_paused
{
	oldcopied='0'
	newcopied='1'
	# wait until reflow copied value stops changing
	while [[ $oldcopied != $newcopied ]]; do
		oldcopied=$newcopied
		sleep 1
		newcopied=$(zpool status $TESTPOOL | \
		    grep 'copied out of' | \
		    awk '{print $1}')
	done
}

log_onexit cleanup

log_must set_tunable32 EMBEDDED_SLOG_MIN_MS 99999

# Disk files which will be used by pool
for i in {0..$(($devs))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

nparity=$((RANDOM%(3) + 1))
raid=raidz$nparity
pool=$TESTPOOL
opts="-o cachefile=none"

log_must zpool create -f $opts $pool $raid ${disks[1..$(($nparity+1))]}

log_must zfs create -o recordsize=8k $pool/fs
log_must fill_fs /$pool/fs 1 256 102400 1 R

log_must zfs create -o recordsize=128k $pool/fs2
log_must fill_fs /$pool/fs2 1 256 102400 1 R

for disk in ${disks[$(($nparity+2))..$devs]}; do
	log_must mkfile -n 400m /$pool/fs/file
	log_bkgrnd randwritecomp /$pool/fs/file 250
	pid0=$!

	# start some random writes in the background during expansion
	log_must mkfile -n 400m /$pool/fs2/file2
	log_bkgrnd randwritecomp /$pool/fs2/file2 250
	pid1=$!
	sleep 10

	# Pause at half total bytes to be copied for expansion
	reflow_size=$(get_pool_prop allocated $pool)
	log_note need to reflow $reflow_size bytes
	pause=$((reflow_size/2))
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $pause

	log_must zpool attach $pool ${raid}-0 $disk
	wait_expand_paused

	kill_if_running $pid0
	kill_if_running $pid1

	log_must zpool scrub -w $pool

	log_must check_pool_status $pool "errors" "No known data errors"
	log_must check_pool_status $pool "scan" "with 0 errors"
	log_must check_pool_status $pool "scan" "repaired 0B"

	# Set pause past largest possible value for this pool
	pause=$((devs*dev_size_mb*1024*1024))
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $pause

	log_must zpool wait -t raidz_expand $pool
done

log_must zpool destroy "$pool"

log_pass "raidz expansion test succeeded."

