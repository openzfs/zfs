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
#	L2ARC header pruning grows exponentially from 1% to the configured
#	ceiling.  Recovery and disabling pruning reset the shared ramp.
#
# STRATEGY:
#	1. Fill two cache devices and reimport to restore L2ARC-only headers.
#	2. Keep pruning disabled beyond the ramp duration and verify no loss.
#	3. Enable pruning and verify a low initial rate and a higher rate after
#	   the ramp duration, within the combined per-device ceilings.
#	4. Restore the header limit and verify a new overage restarts the ramp.
#	5. Disable pruning and verify re-enabling also restarts the ramp.
#	6. Set the duration to zero and verify the maximum rate applies at once,
#	   excess headers are pruned, and feeding resumes.
#

verify_runnable "global"

log_assert "L2ARC header pruning ramps up and resets after recovery."

function cleanup
{
	kstat -g arcstats
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	[[ -n "$caches" ]] && log_must rm -f $caches

	restore_tunable L2ARC_WRITE_MAX
	restore_tunable L2ARC_HDR_PRUNE_RATE_PERCENT
	restore_tunable L2ARC_HDR_PRUNE_RAMP_SECS
	restore_tunable L2ARC_META_PERCENT
	restore_tunable L2ARC_DWPD_LIMIT
	restore_tunable L2ARC_NOPREFETCH
	restore_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE
}
log_onexit cleanup

save_tunable L2ARC_WRITE_MAX
save_tunable L2ARC_HDR_PRUNE_RATE_PERCENT
save_tunable L2ARC_HDR_PRUNE_RAMP_SECS
save_tunable L2ARC_META_PERCENT
save_tunable L2ARC_DWPD_LIMIT
save_tunable L2ARC_NOPREFETCH
save_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE

typeset meta_percent=$(get_tunable L2ARC_META_PERCENT)
typeset caches="$VDIR/cache0 $VDIR/cache1"
typeset -i mib=$((1024 * 1024))
typeset -i ramp_secs=12
typeset -i percent=25
typeset -i ceiling=$((8 * mib * 2 * percent / 100))

log_must set_tunable64 L2ARC_WRITE_MAX $((8 * mib))
log_must set_tunable64 L2ARC_HDR_PRUNE_RATE_PERCENT 0
log_must set_tunable32 L2ARC_HDR_PRUNE_RAMP_SECS $ramp_secs
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
typeset -i prune_bytes=$(kstat arcstats.l2_prune_bytes)
log_must test $hdr_size -gt 0

# Disabling pruning must hold beyond the full ramp duration.
log_must set_tunable32 L2ARC_META_PERCENT 0
log_must sleep $((ramp_secs + 2))
log_must test $(kstat arcstats.l2_hdr_pruned) -eq $pruned
log_must test $(kstat arcstats.l2_prune_bytes) -eq $prune_bytes

# Sample the beginning of a fresh ramp.  Its first few accounting windows
# remain below 10% of the ceiling, including sampling and feeder jitter.
function check_initial_rate
{
	typeset -i before=$(kstat arcstats.l2_prune_bytes)
	typeset -i started=$SECONDS
	log_must sleep 3
	typeset -i bytes=$(( $(kstat arcstats.l2_prune_bytes) - before ))
	typeset -i elapsed=$((SECONDS - started))
	log_note "Initial ramp pruned $bytes bytes in $elapsed seconds"
	log_must test $bytes -gt 0
	log_must test $bytes -le $((ceiling / 10 * (elapsed + 2)))
}

function check_maximum_rate
{
	typeset -i before=$(kstat arcstats.l2_prune_bytes)
	typeset -i started=$SECONDS
	log_must sleep 3
	typeset -i bytes=$(( $(kstat arcstats.l2_prune_bytes) - before ))
	typeset -i elapsed=$((SECONDS - started))
	log_note "Maximum rate pruned $bytes bytes in $elapsed seconds"
	log_must test $bytes -ge $ceiling
	log_must test $bytes -le $((ceiling * (elapsed + 2)))
}

log_must set_tunable64 L2ARC_HDR_PRUNE_RATE_PERCENT $percent
check_initial_rate
log_must sleep $ramp_secs
check_maximum_rate
log_must test $(kstat arcstats.l2_hdr_size) -gt 0

# Recovery resets the ramp shared by both devices.
log_must set_tunable32 L2ARC_META_PERCENT $meta_percent
log_must sleep 2
log_must set_tunable32 L2ARC_META_PERCENT 0
check_initial_rate

# A zero rate resets the ramp and stops counting discarded bytes.
log_must set_tunable64 L2ARC_HDR_PRUNE_RATE_PERCENT 0
log_must sleep 1
pruned=$(kstat arcstats.l2_hdr_pruned)
prune_bytes=$(kstat arcstats.l2_prune_bytes)
log_must sleep 3
log_must test $(kstat arcstats.l2_hdr_pruned) -eq $pruned
log_must test $(kstat arcstats.l2_prune_bytes) -eq $prune_bytes
log_must set_tunable64 L2ARC_HDR_PRUNE_RATE_PERCENT $percent
check_initial_rate

# Zero duration applies the ceiling immediately, even partway into a ramp.
typeset -i feeds=$(kstat arcstats.l2_feeds)
log_must set_tunable32 L2ARC_HDR_PRUNE_RAMP_SECS 0
check_maximum_rate
log_must wait_arcstat l2_hdr_size -eq 0 60
log_must wait_arcstat l2_feeds -gt $feeds 60

log_must zpool destroy -f $TESTPOOL

log_pass "L2ARC header pruning ramps up and resets after recovery."
