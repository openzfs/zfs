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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 by MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A deadlist ZAP that has been damaged so two entries decode to the same
# mintxg used to trip an assertion in dsl_deadlist_load_tree() (avl_add) and
# panic the syncing thread, wedging the pool on import and on snapshot
# destroy (#16685).  With the recovery tunable (zfs_recover) enabled the
# canonically-named entry is kept, the stray name is deleted from disk, and
# the pool remains usable instead.
#
# STRATEGY:
# For each destroy ordering (damaged snapshot's neighbor first, damaged
# snapshot itself first), on a fresh pool:
# 1. Create a pool on a file vdev and churn a dataset so a middle snapshot's
#    deadlist holds real entries.
# 2. Export the pool and inject a duplicate mintxg entry into that snapshot's
#    deadlist with "zhack deadlist dup".
# 3. With zfs_recover enabled, import the pool and destroy the snapshots in
#    the given order, confirming each destroy recovers rather than panicking
#    and the pool stays healthy and writable.
# 4. Verify with zdb that the recovered pool's deadlists load cleanly and
#    no blocks were leaked or double-freed.
#

verify_runnable "global"

# Keep the vdev in its own directory: the leak check below runs
# "zdb -e -p <dir>", which scans every label in <dir> and matches pools by
# name.  $TEST_BASE_DIR is shared with the test suite's own file vdevs (which
# back a pool also named $TESTPOOL), so pointing zdb at it would find more than
# one matching pool and fail before checking anything.
typeset VDEVDIR=$TEST_BASE_DIR/zhack_deadlist_dup
typeset VDEV=$VDEVDIR/vdev
typeset FS=$TESTPOOL/fs
typeset saved_recover=""

function cleanup
{
	[[ -n $saved_recover ]] && set_tunable32 RECOVER $saved_recover
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDEVDIR
}

log_assert "A duplicate mintxg in a deadlist is recovered instead of panicking"
log_onexit cleanup

# Enable recovery so a damaged deadlist is repaired rather than panicking
# the pool.  The default (fatal) behavior is intentionally left unchanged,
# so recovery must be requested explicitly.
saved_recover=$(get_tunable RECOVER)
log_must set_tunable32 RECOVER 1

# test_order <first snapshot to destroy> <second snapshot to destroy>
function test_order
{
	typeset first=$1
	typeset second=$2

	log_must mkdir -p $VDEVDIR
	log_must truncate -s 512M $VDEV
	log_must zpool create $TESTPOOL $VDEV
	log_must zfs create $FS

	# Churn (write, snapshot, free) so snap2's deadlist has real entries.
	for i in 1 2 3; do
		log_must dd if=/dev/urandom of=/$FS/f bs=1M count=8
		log_must zpool sync $TESTPOOL
		log_must zfs snapshot $FS@snap$i
		log_must rm -f /$FS/f
	done
	log_must zpool sync $TESTPOOL

	log_must zpool export $TESTPOOL

	# Inject a second deadlist entry whose name decodes to an existing
	# mintxg of snap2's deadlist.
	log_must zhack -d $VDEV deadlist dup $TESTPOOL $FS@snap2

	log_must zpool import -d $VDEV $TESTPOOL

	# Destroying the neighbor loads snap2's damaged deadlist via
	# dsl_deadlist_move_bpobj(), which must recover, keep the canonical
	# entry, and delete the stray name from disk.  Destroying snap2
	# itself merges its damaged deadlist (dsl_deadlist_merge), which
	# must fold in only the canonical entry.  Both orderings must
	# succeed rather than panic the syncing thread.
	log_must zfs destroy $FS@$first
	log_must zfs destroy $FS@$second
	log_must datasetexists $FS@snap3

	# The pool and the remaining snapshot must still be intact and
	# writable.
	log_must dd if=/dev/urandom of=/$FS/after bs=1M count=4
	log_must zpool sync $TESTPOOL
	log_must check_state $TESTPOOL "" "ONLINE"
	log_must eval "zpool status -x $TESTPOOL | grep -q 'is healthy'"

	# The damage must be gone from disk: zdb loads every deadlist (a
	# surviving stray would reference a dangling object and fail), and
	# the leak check confirms nothing was leaked or double-freed.
	log_must zpool export $TESTPOOL
	log_must eval "zdb -e -p $VDEVDIR -b $TESTPOOL | \
	    grep -q 'No leaks'"

	log_must zpool import -d $VDEV $TESTPOOL
	log_must destroy_pool $TESTPOOL
	log_must rm -rf $VDEVDIR
}

# (a) destroy the damaged snapshot's neighbor first, then the snapshot itself
test_order snap1 snap2
# (b) destroy the damaged snapshot first
test_order snap2 snap1

log_pass "A duplicate mintxg in a deadlist is recovered instead of panicking"
