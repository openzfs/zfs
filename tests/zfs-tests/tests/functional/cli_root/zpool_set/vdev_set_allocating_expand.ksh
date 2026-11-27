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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A top-level vdev with allocating=off that grows is still accounted
#	once in the space the pool keeps off-limits, so the available space
#	of the pool does not change.
#
# STRATEGY:
#	1. Create a pool with a mirror, add a disk and set allocating=off on
#	   it.
#	2. Grow the disk with "zpool online -e" and verify that the available
#	   space did not change.
#	3. Set allocating=on and verify that the available space is the one
#	   seen after an export and import.
#	4. Add a raidz1 vdev, set allocating=off on it, expand it with
#	   "zpool attach" and verify that the available space did not change.
#	5. Repeat step 3 for the raidz1 vdev.
#

verify_runnable "global"

typeset POOL=alloc_expand_pool
typeset DIR=$TEST_BASE_DIR/vdev_alloc_expand.$$
typeset TOLERANCE=$((32 * 1024 * 1024))

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -rf $DIR
}

# sync_pool logs to stdout, keep only the value.
function avail
{
	sync_pool $POOL >/dev/null
	get_prop available $POOL
}

# Fail if two space values differ by more than the tolerance.
function check_same_space # what before after
{
	log_note "$1: available space $2 -> $3"
	[[ $2 == +([0-9]) && $3 == +([0-9]) ]] ||
	    log_fail "$1: available space is not a number: '$2' '$3'"
	awk -v a="$2" -v b="$3" -v t="$TOLERANCE" \
	    'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= t) }' ||
	    log_fail "$1: available space went from $2 to $3"
}

# The space of a grown vdev is added asynchronously, wait for it.
function wait_vdev_growth # vdev size_before
{
	typeset -i tries=0

	while (( tries++ < 60 )); do
		sync_pool $POOL
		(( $(get_vdev_prop size $POOL $1) > $2 )) && return 0
		sleep 1
	done
	log_fail "$1 did not grow beyond $2"
}

function check_allocating_on # vdev
{
	typeset on imported

	log_must zpool set allocating=on $POOL $1
	on=$(avail)
	log_must zpool export $POOL
	log_must zpool import -d $DIR $POOL
	imported=$(avail)
	check_same_space "allocating=on on $1, then import" $on $imported
}

log_onexit cleanup

log_assert "a vdev with allocating=off is accounted once when it grows"

log_must mkdir -p $DIR
log_must truncate -s 1G $DIR/m0 $DIR/m1
log_must truncate -s 256M $DIR/d0 $DIR/r0 $DIR/r1 $DIR/r2 $DIR/r3
log_must zpool create -f $POOL mirror $DIR/m0 $DIR/m1

#
# 1-3. A disk grown with "zpool online -e".
#
log_must zpool add -f $POOL $DIR/d0
log_must zpool set allocating=off $POOL $DIR/d0
before=$(avail)
size=$(get_vdev_prop size $POOL $DIR/d0)
log_must truncate -s 512M $DIR/d0
log_must zpool online -e $POOL $DIR/d0
wait_vdev_growth $DIR/d0 $size
check_same_space "zpool online -e" $before "$(avail)"
check_allocating_on $DIR/d0

#
# 4-5. A raidz1 vdev grown by a raidz expansion.
#
log_must zpool add -f $POOL raidz1 $DIR/r0 $DIR/r1 $DIR/r2
log_must zpool set allocating=off $POOL raidz1-2
before=$(avail)
size=$(get_vdev_prop size $POOL raidz1-2)
log_must zpool attach $POOL raidz1-2 $DIR/r3
log_must zpool wait -t raidz_expand $POOL
wait_vdev_growth raidz1-2 $size
check_same_space "raidz expansion" $before "$(avail)"
check_allocating_on raidz1-2

log_pass "a vdev with allocating=off is accounted once when it grows"
