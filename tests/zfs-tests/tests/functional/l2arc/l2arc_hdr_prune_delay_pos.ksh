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
#	Brief header overages do not prune the L2ARC.  Sustained overages are
#	pruned after a grace period.
#
# STRATEGY:
#	1. Fill two cache devices and reimport to restore L2ARC-only headers.
#	2. Lower the header limit twice, restoring it between the overages.
#	   Verify neither overage prunes headers during its grace period.
#	3. Disable pruning with a zero budget and leave the lower limit in
#	   place longer than the grace period.  Verify no headers are pruned.
#	4. Enable pruning.  Verify it waits for a new grace period, prunes the
#	   excess headers, and resumes feeding.
#

verify_runnable "global"

log_assert "L2ARC header pruning waits for sustained overages."

function cleanup
{
	kstat -g arcstats
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	[[ -n "$caches" ]] && log_must rm -f $caches

	restore_tunable L2ARC_HDR_PRUNE_BATCH_LIMIT
	restore_tunable L2ARC_HDR_PRUNE_BUDGET
	restore_tunable L2ARC_HDR_PRUNE_DELAY
	restore_tunable L2ARC_META_PERCENT
	restore_tunable L2ARC_DWPD_LIMIT
	restore_tunable L2ARC_NOPREFETCH
	restore_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE
}
log_onexit cleanup

save_tunable L2ARC_HDR_PRUNE_BATCH_LIMIT
save_tunable L2ARC_HDR_PRUNE_BUDGET
save_tunable L2ARC_HDR_PRUNE_DELAY
save_tunable L2ARC_META_PERCENT
save_tunable L2ARC_DWPD_LIMIT
save_tunable L2ARC_NOPREFETCH
save_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE

typeset meta_percent=$(get_tunable L2ARC_META_PERCENT)
typeset caches="$VDIR/cache0 $VDIR/cache1"
typeset -i mib=$((1024 * 1024))
typeset -i delay=8

log_must set_tunable32 L2ARC_HDR_PRUNE_BATCH_LIMIT 256
log_must set_tunable64 L2ARC_HDR_PRUNE_BUDGET 65536
log_must set_tunable32 L2ARC_HDR_PRUNE_DELAY $delay
log_must set_tunable32 L2ARC_DWPD_LIMIT 0
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable64 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE 0

log_must rm -f $VDEV $VDEV_CACHE $caches
log_must truncate -s $SIZE $VDEV
log_must truncate -s 256M $caches
log_must zpool create -f -O recordsize=16K $TESTPOOL $VDEV cache $caches
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=128
sync_pool $TESTPOOL
log_must wait_arcstat l2_size -ge $((64 * mib)) 120
log_must arcstat_quiescence_noecho l2_size 60

typeset -i rebuild_success=$(kstat arcstats.l2_rebuild_success)
log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL
log_must wait_arcstat l2_rebuild_success -ge $((rebuild_success + 2)) 60
log_must arcstat_quiescence_noecho l2_size 60

typeset -i hdr_size=$(kstat arcstats.l2_hdr_size)
typeset -i pruned=$(kstat arcstats.l2_hdr_pruned)
log_must test $hdr_size -gt 0

# Five seconds is shorter than the grace period.  Two such overages would
# exceed it if recovery did not reset the timer.
log_must set_tunable32 L2ARC_META_PERCENT 0
log_must sleep 5
log_must test $(kstat arcstats.l2_hdr_pruned) -eq $pruned
log_must set_tunable32 L2ARC_META_PERCENT $meta_percent
log_must sleep 2
log_must set_tunable32 L2ARC_META_PERCENT 0
log_must sleep 5
log_must test $(kstat arcstats.l2_hdr_pruned) -eq $pruned

# A zero budget disables pruning and resets the timer.
log_must set_tunable64 L2ARC_HDR_PRUNE_BUDGET 0
log_must sleep $((delay + 2))
log_must test $(kstat arcstats.l2_hdr_pruned) -eq $pruned

# Re-enabling pruning must start a new grace period.
typeset -i feeds=$(kstat arcstats.l2_feeds)
log_must set_tunable64 L2ARC_HDR_PRUNE_BUDGET 65536
log_must sleep 5
log_must test $(kstat arcstats.l2_hdr_pruned) -eq $pruned
log_must wait_arcstat l2_hdr_pruned -gt $pruned $((delay + 2))
log_must wait_arcstat l2_feeds -gt $feeds 60
log_must wait_arcstat l2_hdr_size -eq 0 30

log_must zpool destroy -f $TESTPOOL

log_pass "L2ARC header pruning waits for sustained overages."
