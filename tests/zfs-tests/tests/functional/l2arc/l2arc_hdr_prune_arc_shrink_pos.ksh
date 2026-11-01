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
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg

#
# DESCRIPTION:
#	When the ARC shrinks, L2ARC-only headers over the lower
#	l2arc_meta_percent limit are pruned down to the limit, not below it,
#	and the L2ARC caches new data again once the ARC grows back.
#
# STRATEGY:
#	1. Hold the ARC between 448 and 512 MiB, so the limit stays above the
#	   headers until the shrink, and set l2arc_meta_percent to 1.
#	2. Fill the L2ARC with small records, then export and import the pool,
#	   so the rebuild restores them as L2ARC-only headers.
#	3. Shrink the ARC to 128 MiB, so the headers exceed the new limit.
#	   Verify they are pruned to the limit but not to zero, and that the
#	   L2ARC resumes feeding.
#	4. Grow the ARC back to 512 MiB and write more data.  Verify the
#	   L2ARC caches it.
#

verify_runnable "global"

log_assert "L2ARC-only headers are pruned to the limit when the ARC shrinks."

function cleanup
{
	kstat -g arcstats
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	# Set the ARC sizes to their recorded values and then the tunables to
	# their original values, which also works for unset tunables.  Lower
	# the minimum first, in case the test failed with it raised.
	log_must set_tunable64 ARC_MIN $((64 * 1024 * 1024))
	log_must set_tunable64 ARC_MAX $c_max
	log_must set_tunable64 ARC_MIN $c_min
	restore_tunable ARC_MAX
	restore_tunable ARC_MIN
	restore_tunable L2ARC_META_PERCENT
	restore_tunable L2ARC_DWPD_LIMIT
	restore_tunable L2ARC_NOPREFETCH
	restore_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE
}
log_onexit cleanup

typeset c_max=$(kstat arcstats.c_max)
typeset c_min=$(kstat arcstats.c_min)
save_tunable ARC_MAX
save_tunable ARC_MIN
save_tunable L2ARC_META_PERCENT
save_tunable L2ARC_DWPD_LIMIT
save_tunable L2ARC_NOPREFETCH
save_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE

typeset -i mib=$((1024 * 1024))
typeset -i arc_big=$((512 * mib))
typeset -i arc_small=$((128 * mib))
typeset -i small_limit=$((arc_small / 100))

# Lower the minimum first, since arc_max must stay above it.
log_must set_tunable64 ARC_MIN $((64 * mib))
log_must set_tunable64 ARC_MAX $arc_big
log_must set_tunable64 ARC_MIN $((448 * mib))
log_must set_tunable32 L2ARC_META_PERCENT 1
log_must set_tunable32 L2ARC_DWPD_LIMIT 0
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable64 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE 0

# Discard backing-file contents and cached pages left by earlier tests.
log_must rm -f $VDEV $VDEV_CACHE
log_must truncate -s $SIZE $VDEV
log_must truncate -s 1G $VDEV_CACHE
log_must zpool create -f -O recordsize=4K $TESTPOOL $VDEV cache $VDEV_CACHE

# 128 MiB of 4K records makes about 3 MiB of L2ARC-only headers.
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=128
sync_pool $TESTPOOL
# Cache enough to leave headroom above the 1.28 MiB limit after shrinking.
log_must wait_arcstat l2_size -ge $(( 96 * mib )) 120

typeset -i rebuild_success=$(kstat arcstats.l2_rebuild_success)
log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL
log_must wait_arcstat l2_rebuild_success -gt $rebuild_success 60
typeset -i hdr_size=$(kstat arcstats.l2_hdr_size)
log_note "restored $hdr_size bytes of headers, limit after shrink $small_limit"
log_must test $hdr_size -gt $small_limit

typeset -i pruned=$(kstat arcstats.l2_hdr_pruned)
log_must set_tunable64 ARC_MIN $((64 * mib))
log_must set_tunable64 ARC_MAX $arc_small
log_must wait_arcstat l2_hdr_pruned -gt $pruned 60
log_must wait_arcstat l2_hdr_size -le $small_limit 60
hdr_size=$(kstat arcstats.l2_hdr_size)
log_note "pruned $(( $(kstat arcstats.l2_hdr_pruned) - pruned )) headers," \
    "$hdr_size bytes left"
log_must test $hdr_size -le $small_limit
log_must test $hdr_size -gt 0

typeset -i feeds=$(kstat arcstats.l2_feeds)
log_must wait_arcstat l2_feeds -gt $feeds 30

log_must set_tunable64 ARC_MAX $arc_big
typeset -i l2_size=$(kstat arcstats.l2_size)
log_must dd if=/dev/urandom of=/$TESTPOOL/more bs=1M count=64
sync_pool $TESTPOOL
log_must wait_arcstat l2_size -gt $l2_size 60

log_must zpool destroy -f $TESTPOOL

log_pass "L2ARC-only headers are pruned to the limit when the ARC shrinks."
