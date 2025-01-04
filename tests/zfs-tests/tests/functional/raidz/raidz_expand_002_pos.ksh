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
# Copyright (c) 2020 by vStack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zpool attach poolname raidz ...' should attach new devive to the pool.
#
# STRATEGY:
#	1. Create block device files for the test raidz pool
#	2. For each parity value [1..3]
#	    - create raidz pool with minimum block device files required
#	    - for each free test block device
#	        - attach to the pool
#	        - verify the raidz pool
#	    - destroy the raidz pool

typeset -r devs=6
typeset -r dev_size_mb=512

typeset -a disks

prefetch_disable=$(get_tunable PREFETCH_DISABLE)

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done

	log_must set_tunable32 PREFETCH_DISABLE $prefetch_disable
}

log_onexit cleanup

log_must set_tunable32 PREFETCH_DISABLE 1

# Disk files which will be used by pool
for i in {0..$(($devs))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

nparity=$((RANDOM%(3) + 1))
raid=raidz$nparity
dir=$TEST_BASE_DIR
pool=$TESTPOOL
opts="-o cachefile=none"

log_must zpool create -f $opts $pool $raid ${disks[1..$(($nparity+1))]}
log_must zfs set primarycache=metadata $pool

log_must zfs create $pool/fs
log_must fill_fs /$pool/fs 1 512 102400 1 R

log_must zfs create -o compress=on $pool/fs2
log_must fill_fs /$pool/fs2 1 512 102400 1 R

log_must zfs create -o compress=on -o recordsize=8k $pool/fs3
log_must fill_fs /$pool/fs3 1 512 102400 1 R

typeset pool_size=$(get_pool_prop size $pool)

for disk in ${disks[$(($nparity+2))..$devs]}; do
	log_must dd if=/dev/urandom of=/${pool}/FILE-$RANDOM bs=1M \
	    count=64

	log_must zpool attach -w $pool ${raid}-0 $disk

	# Wait some time for pool size increase
	sleep 5

	# Confirm that disk was attached to the pool
	log_must zpool get -H path $TESTPOOL $disk

	typeset expand_size=$(get_pool_prop size $pool)
	if [[ "$expand_size" -le "$pool_size" ]]; then
		log_fail "pool $pool not expanded"
	fi

	is_pool_scrubbing $pool && wait_scrubbed $pool
	verify_pool $pool

	pool_size=$expand_size
done

zpool destroy "$pool"

log_pass "raidz expansion test succeeded."
