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
#	L2ARC-only headers over l2arc_meta_percent are pruned, which lets the
#	L2ARC resume feeding.  Headers of buffers still in the ARC are kept,
#	and the pruned buffers stay on the cache device.  Cache device
#	operations work while pruning is under way.
#
# STRATEGY:
#	1. Create a pool with several cache devices and fill the L2ARC with two
#	   files.
#	2. Export and import the pool, so the rebuild restores the L2ARC as
#	   L2ARC-only headers.
#	3. Read one file back, so its headers have an L1 part again.
#	4. Lower l2arc_meta_percent to 0 with pruning disabled.  Verify the
#	   headers are kept and the L2ARC stops feeding.
#	5. Enable pruning with a small budget and batch size.  Verify the
#	   L2ARC-only headers are pruned across several feed iterations within
#	   the budget, the headers of the file read back are kept, and feeding
#	   resumes.
#	6. Restore l2arc_meta_percent and write a third file.  Verify the
#	   L2ARC caches it.
#	7. Export and import the pool.  Verify the pruned file is read from
#	   the L2ARC without errors.
#	8. Prune slowly again.  While pruning is under way, take a cache device
#	   offline and back online, remove and re-add another, and export and
#	   import the pool.  Verify the files are intact, the L2ARC reported no
#	   errors, and a scrub is clean.
#

verify_runnable "global"

log_assert "L2ARC-only headers over l2arc_meta_percent are pruned."

function cleanup
{
	kstat -g arcstats
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	[[ -n "$caches" ]] && log_must rm -f $caches

	restore_tunable L2ARC_HDR_PRUNE_BATCH_LIMIT
	restore_tunable L2ARC_HDR_PRUNE_BUDGET
	restore_tunable L2ARC_META_PERCENT
	restore_tunable L2ARC_NOPREFETCH
	restore_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE
}
log_onexit cleanup

save_tunable L2ARC_HDR_PRUNE_BATCH_LIMIT
save_tunable L2ARC_HDR_PRUNE_BUDGET
save_tunable L2ARC_META_PERCENT
save_tunable L2ARC_NOPREFETCH
save_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE

typeset meta_percent=$(get_tunable L2ARC_META_PERCENT)

log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable64 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE 0

typeset fill_mb=64
typeset recsize=16384
typeset budget=128
typeset batch=16
typeset -i ndevs=2
typeset caches=""
typeset -i i

for (( i = 0; i < ndevs; i++ )); do
	caches="$caches $VDIR/cache$i"
done
# Also discard the shared cache file, even though this test does not use it.
log_must rm -f $VDEV $VDEV_CACHE $caches
log_must truncate -s $SIZE $VDEV
log_must truncate -s 256M $caches
log_must zpool create -f -O recordsize=$recsize $TESTPOOL $VDEV \
	cache $caches

log_must dd if=/dev/urandom of=/$TESTPOOL/kept bs=1M count=$fill_mb
log_must dd if=/dev/urandom of=/$TESTPOOL/pruned bs=1M count=$fill_mb
sync_pool $TESTPOOL
# Wait for the L2ARC to cache at least half of the data, which takes
# several feed iterations.
log_must wait_arcstat l2_size -ge $(( fill_mb * 1024 * 1024 )) 120
log_must arcstat_quiescence_noecho l2_size 60
typeset sums=$(cat /$TESTPOOL/kept /$TESTPOOL/pruned | cksum)

typeset rebuild_bufs=$(kstat arcstats.l2_rebuild_bufs)
log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL
log_must wait_arcstat l2_size -gt 0 60
log_must arcstat_quiescence_noecho l2_size 60
log_must test $(kstat arcstats.l2_rebuild_bufs) -gt $rebuild_bufs

typeset l2_hits=$(kstat arcstats.l2_hits)
log_must dd if=/$TESTPOOL/kept of=/dev/null bs=1M
typeset kept_bytes=$(( ($(kstat arcstats.l2_hits) - l2_hits) * recsize ))
log_must test $kept_bytes -gt 0

# Stop feeding without pruning, and let any write in progress finish.
log_must set_tunable64 L2ARC_HDR_PRUNE_BUDGET 0
log_must set_tunable32 L2ARC_META_PERCENT 0
log_must sleep 2

typeset hdr_size=$(kstat arcstats.l2_hdr_size)
typeset l2_size=$(kstat arcstats.l2_size)
typeset feeds=$(kstat arcstats.l2_feeds)
typeset abort_lowmem=$(kstat arcstats.l2_abort_lowmem)
typeset pruned=$(kstat arcstats.l2_hdr_pruned)
log_must test $hdr_size -gt 0
log_must sleep 5
log_must test $(kstat arcstats.l2_hdr_size) -eq $hdr_size
log_must test $(kstat arcstats.l2_feeds) -eq $feeds
log_must test $(kstat arcstats.l2_abort_lowmem) -gt $abort_lowmem
log_must test $(kstat arcstats.l2_hdr_pruned) -eq $pruned

# Each device prunes up to the budget per aborted feed iteration, so pruning
# more than the budget times the device count means some device's scan
# resumed where it left off.  A batch smaller than the budget also makes each
# iteration drop and retake the buffer list lock between batches.
abort_lowmem=$(kstat arcstats.l2_abort_lowmem)
log_must set_tunable32 L2ARC_HDR_PRUNE_BATCH_LIMIT $batch
log_must set_tunable64 L2ARC_HDR_PRUNE_BUDGET $budget
log_must wait_arcstat l2_feeds -gt $feeds 120
typeset -i npruned=$(( $(kstat arcstats.l2_hdr_pruned) - pruned ))
typeset -i iters=$(( $(kstat arcstats.l2_abort_lowmem) - abort_lowmem ))
log_note "l2_hdr_size $hdr_size -> $(kstat arcstats.l2_hdr_size)"
log_note "l2_size $l2_size -> $(kstat arcstats.l2_size), kept $kept_bytes"
log_note "pruned $npruned headers in $iters iterations"
log_must test $(kstat arcstats.l2_hdr_size) -lt $hdr_size
log_must test $npruned -gt $(( budget * ndevs ))
log_must test $npruned -le $(( iters * budget ))
log_must test $(kstat arcstats.l2_size) -lt $l2_size
log_must test $(kstat arcstats.l2_size) -ge $(( kept_bytes / 2 ))
log_must eval "zarcsummary -s l2arc | grep -q 'Pruned headers'"

# Once the headers fit again, the L2ARC caches new data.
log_must set_tunable32 L2ARC_META_PERCENT $meta_percent
l2_size=$(kstat arcstats.l2_size)
log_must dd if=/dev/urandom of=/$TESTPOOL/refill bs=1M count=$fill_mb
sync_pool $TESTPOOL
log_must wait_arcstat l2_size -gt $l2_size 60

log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL
log_must wait_arcstat l2_size -gt 0 60
log_must arcstat_quiescence_noecho l2_size 60

typeset cksum_bad=$(kstat arcstats.l2_cksum_bad)
typeset io_error=$(kstat arcstats.l2_io_error)
l2_hits=$(kstat arcstats.l2_hits)
log_must dd if=/$TESTPOOL/pruned of=/dev/null bs=1M
log_must test $(kstat arcstats.l2_hits) -gt $l2_hits
log_must test $(kstat arcstats.l2_cksum_bad) -eq $cksum_bad
log_must test $(kstat arcstats.l2_io_error) -eq $io_error

# A small budget keeps every device's scan part way through its buffer list
# while the operations run.
log_must set_tunable64 L2ARC_HDR_PRUNE_BUDGET 16
pruned=$(kstat arcstats.l2_hdr_pruned)
log_must set_tunable32 L2ARC_META_PERCENT 0
log_must wait_arcstat l2_hdr_pruned -gt $(( pruned + ndevs * 64 )) 60

# The reopen on online evicts the device, which removes its marker.
log_must zpool offline $TESTPOOL $VDIR/cache0
log_must check_state $TESTPOOL $VDIR/cache0 "offline"
log_must zpool online $TESTPOOL $VDIR/cache0
log_must check_state $TESTPOOL $VDIR/cache0 "online"

# Removal and export tear devices down with their markers in place.
log_must zpool remove $TESTPOOL $VDIR/cache1
log_must zpool add -f $TESTPOOL cache $VDIR/cache1
log_must check_state $TESTPOOL $VDIR/cache1 "online"
log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL
log_must set_tunable32 L2ARC_META_PERCENT $meta_percent

log_must test "$(cat /$TESTPOOL/kept /$TESTPOOL/pruned | cksum)" = "$sums"
log_must test $(kstat arcstats.l2_cksum_bad) -eq $cksum_bad
log_must test $(kstat arcstats.l2_io_error) -eq $io_error
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_must zpool destroy -f $TESTPOOL

log_pass "L2ARC-only headers over l2arc_meta_percent are pruned."
