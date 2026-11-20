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
#	A vdev property call that sets several properties applies none of
#	them when one is refused, in memory as on disk.
#
# STRATEGY:
#	1. Create a pool of two file vdevs.
#	2. In one call, set the alloc_bias of the first to special and its
#	   path to an invalid value: the call fails, and alloc_bias, which
#	   zpool get reads from memory, is still none.
#	3. Set alloc_bias to special, then in one call set it back to none
#	   along with the invalid path: it is still special.
#	4. Stop allocations on the second vdev, then in one call set the
#	   alloc_bias of the first to none and stop its allocations too:
#	   the last allocating vdev refuses, and alloc_bias is still special.
#	5. In one call, set checksum_n and io_n: both are set.
#	6. What the sync task could not write is refused, and nothing is
#	   set: a user property sent as a number, along with an alloc_bias,
#	   an index that its property does not have, and a value too long
#	   for the ZAP.
#	7. In a pool of two larger file vdevs, reserve the space that the
#	   default slop leaves free, then raise the slop: the sync task
#	   now fails with ENOSPC, and the alloc_bias that the call sets is
#	   still none in memory.
#

verify_runnable "global"

function cleanup
{
	restore_tunable SPA_SLOP_SHIFT
	destroy_pool $TESTPOOL1
	rm -f $FILEDEV0 $FILEDEV1
}

function check_bias # expected
{
	typeset bias=$(zpool get -H -o value alloc_bias $TESTPOOL1 $FILEDEV0)

	[[ "$bias" == "$1" ]] || log_fail "alloc_bias is '$bias', expected '$1'"
}

log_assert "A vdev property call with a refused property applies none"
log_onexit cleanup

FILEDEV0="$TEST_BASE_DIR/filedev0.$$"
FILEDEV1="$TEST_BASE_DIR/filedev1.$$"
log_must truncate -s $MINVDEVSIZE $FILEDEV0 $FILEDEV1
log_must zpool create -f $TESTPOOL1 $FILEDEV0 $FILEDEV1
guid=$(zpool get -H -o value guid $TESTPOOL1 $FILEDEV0)

# alloc_bias 0 is none and 2 is special; a path must be under /dev.
log_mustnot vdev_set_props $TESTPOOL1 $guid alloc_bias=2 path=/invalid
check_bias none

log_must zpool set alloc_bias=special $TESTPOOL1 $FILEDEV0
log_mustnot vdev_set_props $TESTPOOL1 $guid alloc_bias=0 path=/invalid
check_bias special

# A new alloc_bias takes effect at the next import, so the first vdev is
# still the last one that allocates; allocating 0 is off.
log_must zpool set allocating=off $TESTPOOL1 $FILEDEV1
log_mustnot vdev_set_props $TESTPOOL1 $guid alloc_bias=0 allocating=0
check_bias special

log_must vdev_set_props $TESTPOOL1 $guid checksum_n=7 io_n=9
log_must test "$(zpool get -H -o value checksum_n $TESTPOOL1 $FILEDEV0)" = 7
log_must test "$(zpool get -H -o value io_n $TESTPOOL1 $FILEDEV0)" = 9

# The helper sends a value made of digits as a number, and a user property
# must be a string; slow_io_events is 0 or 1; ZAP values stop below 8192.
log_mustnot vdev_set_props $TESTPOOL1 $guid alloc_bias=0 org.zfstest:prop=5
check_bias special
log_mustnot vdev_set_props $TESTPOOL1 $guid slow_io_events=5
log_mustnot vdev_set_props $TESTPOOL1 $guid \
    org.zfstest:prop=$(printf '%8192s' | tr ' ' v)
prop=$(zpool get -H -o value org.zfstest:prop $TESTPOOL1 $FILEDEV0)
[[ "$prop" == "-" ]] || log_fail "org.zfstest:prop is set"

# The sync task writes the values once it finds more free space than a
# quarter of the slop; spa_slop_shift=1 makes the slop half the pool.
log_must destroy_pool $TESTPOOL1
log_must rm -f $FILEDEV0 $FILEDEV1
log_must truncate -s 2G $FILEDEV0 $FILEDEV1
log_must zpool create -f $TESTPOOL1 $FILEDEV0 $FILEDEV1
guid=$(zpool get -H -o value guid $TESTPOOL1 $FILEDEV0)
typeset -li avail=$(get_prop available $TESTPOOL1)
log_must zfs create -o refreservation=$((avail - 64 * 1024 * 1024)) \
    $TESTPOOL1/resv
log_must save_tunable SPA_SLOP_SHIFT
log_must set_tunable32 SPA_SLOP_SHIFT 1
out=$(vdev_set_props $TESTPOOL1 $guid alloc_bias=2 2>&1) && \
    log_fail "alloc_bias was set although the pool is out of space"
log_must restore_tunable SPA_SLOP_SHIFT
[[ "$out" == *"No space left"* ]] || \
    log_fail "the call failed with '$out' instead of ENOSPC"
check_bias none

log_pass "A vdev property call with a refused property applies none"
