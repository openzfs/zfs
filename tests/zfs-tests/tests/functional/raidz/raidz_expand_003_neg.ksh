#!/bin/ksh -p
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
#	'zpool attach poolname raidz ...' should reject device attach if pool
#	is in checkpointed state. If checkpoint creation requested on
#	expanding pool, the request should be rejected.

#
# STRATEGY:
#	1. Create block device files for the test raidz pool.
#	2. Create pool and checkpoint it.
#	3. Try to expand raidz, ensure that request rejected.
#	4. Recreate the pool.
#	5. Apply raidz expansion.
#	6. Ensure, that checkpoint cannot be created.

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
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

log_onexit cleanup

log_must set_tunable32 PREFETCH_DISABLE 1

# Disk files which will be used by pool
for i in {0..$(($devs))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

nparity=1
raid=raidz$nparity
pool=$TESTPOOL
opts="-o cachefile=none"

# case 1: checkpoint exist, try to expand
log_must zpool create -f $opts $pool $raid ${disks[1..$(($devs-1))]}
log_must zfs set primarycache=metadata $pool
log_must zpool checkpoint $pool
log_mustnot zpool attach $pool ${raid}-0 ${disks[$devs]}
log_must zpool destroy $pool

#
# case 2: expansion in progress, try to checkpoint
#
# Sets pause point at 25% of allocated space so that we know an
# expansion is still in progress when we attempt the checkpoint
#
log_must zpool create -f $opts $pool $raid ${disks[1..$(($devs-1))]}
log_must zfs set primarycache=metadata $pool
log_must zfs create $pool/fs
log_must fill_fs /$pool/fs 1 512 100 1024 R
allocated=$(zpool list -Hp -o allocated $pool)
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((allocated / 4))
log_must zpool attach $pool ${raid}-0 ${disks[$devs]}
log_mustnot zpool checkpoint $pool
log_must zpool destroy $pool

log_pass "raidz expansion test succeeded."
