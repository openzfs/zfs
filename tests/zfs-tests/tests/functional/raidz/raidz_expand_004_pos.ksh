#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2021 by vStack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Check device replacement during raidz expansion.
#
# STRATEGY:
#	1. Create block device files for the test raidz pool
#	2. For each parity value [1..3]
#	    - create raidz pool with minimum block device files required
#	    - create couple of datasets with different recordsize and fill it
#	    - attach new device to the pool
#	    - offline and zero vdevs allowed by parity
#	    - wait some time and start offlined vdevs replacement
#	    - wait replacement completion and verify pool status

typeset -r devs=10
typeset -r dev_size_mb=128

typeset -a disks

embedded_slog_min_ms=$(get_tunable EMBEDDED_SLOG_MIN_MS)
original_scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done

	log_must set_tunable32 EMBEDDED_SLOG_MIN_MS $embedded_slog_min_ms
	log_must set_tunable32 SCRUB_AFTER_EXPAND $original_scrub_after_expand
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

log_must set_tunable32 SCRUB_AFTER_EXPAND 0

log_must zpool create -f $opts $pool $raid ${disks[1..$(($nparity+1))]}

log_must zfs create -o recordsize=8k $pool/fs
log_must fill_fs /$pool/fs 1 128 102400 1 R

log_must zfs create -o recordsize=128k $pool/fs2
log_must fill_fs /$pool/fs2 1 128 102400 1 R

for disk in ${disks[$(($nparity+2))..$devs]}; do
	log_must zpool attach $pool ${raid}-0 $disk

	sleep 10

	for (( i=1; i<=$nparity; i=i+1 )); do
		log_must zpool offline $pool ${disks[$i]}
		log_must dd if=/dev/zero of=${disks[$i]} \
		    bs=1024k count=$dev_size_mb conv=notrunc
	done

	sleep 3

	for (( i=1; i<=$nparity; i=i+1 )); do
		log_must zpool replace $pool ${disks[$i]}
	done

	log_must zpool wait -t replace $pool
	log_must check_pool_status $pool "scan" "with 0 errors"

	log_must zpool wait -t raidz_expand $pool

	log_must zpool clear $pool
	log_must zpool scrub -w $pool

	log_must zpool status -v
	log_must check_pool_status $pool "scan" "with 0 errors"
done

log_must zpool destroy "$pool"

log_pass "raidz expansion test succeeded."

