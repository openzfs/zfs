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
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/scheduler/scheduler.cfg

#
# DESCRIPTION:
#	Benchmark all 4 vdev scheduler modes (auto, off, on, wronly) for
#	read, write, randread, randwrite, randrw workloads using fio
#	(iodepth=64, 30s). Compare IOPS and show the impact of each
#	scheduling policy.
#
# STRATEGY:
#	1. For each scheduler mode (auto, off, on, wronly):
#	   Set mode on all leaf vdevs, then run 4 workloads × 30s each.
#	2. Print a 4-way summary table with percentage changes vs auto.
#

verify_runnable "global"

typeset -r IODEPTH=64
typeset -r RUNTIME=10

#
# Auto-detect the best async I/O engine available on this platform.
# Prefer libaio (Linux), fall back to posixaio (FreeBSD), then psync.
#
typeset IOENGINE
if fio --enghelp 2>/dev/null | grep -q 'libaio'; then
	IOENGINE="libaio"
elif fio --enghelp 2>/dev/null | grep -q 'posixaio'; then
	IOENGINE="posixaio"
else
	IOENGINE="psync"
fi


#
# Run a Direct I/O fio benchmark and return IOPS.
#
# $1: mountpoint
# $2: rw type (read, write, randread, randwrite)
# $3: test filename
# $4: label suffix
#
# Returns IOPS on stdout (field 8 for reads, field 49 for writes).
#
function bench_iops
{
	typeset mntpnt=$1
	typeset rw=$2
	typeset filename=$3
	typeset label=$4

	typeset field
	case "$rw" in
	read|randread)
		field=8     # read IOPS in fio minimal output
		;;
	write|randwrite)
		field=49    # write IOPS in fio minimal output
		;;
	randrw)
		# total IOPS = read + write; capture both and sum below
		;;
	*)
		log_fail "Unknown rw type: $rw"
		;;
	esac

	typeset iops
	if [[ "$rw" == "randrw" ]]; then
		# fio minimal: field 8 = read IOPS, field 49 = write IOPS
		typeset line
		line=$(fio --filename="$filename" --name="$label" \
		    --rw="$rw" --rwmixread=50 \
		    --bs="$SCHEDULER_BS_HR" --size="$SCHEDULER_FILESIZE_HR" \
		    --direct=1 --numjobs=1 --iodepth=$IODEPTH \
		    --ioengine=$IOENGINE --fallocate=none \
		    --group_reporting --minimal --runtime=$RUNTIME --time_based \
		    2>/dev/null | grep ';')
		typeset riops wops
		riops=$(echo "$line" | cut -d';' -f8)
		wops=$(echo "$line" | cut -d';' -f49)
		iops=$(( riops + wops ))
	else
		iops=$(fio --filename="$filename" --name="$label" \
		    --rw="$rw" --bs="$SCHEDULER_BS_HR" --size="$SCHEDULER_FILESIZE_HR" \
		    --direct=1 --numjobs=1 --iodepth=$IODEPTH \
		    --ioengine=$IOENGINE --fallocate=none \
		    --group_reporting --minimal --runtime=$RUNTIME --time_based \
		    2>/dev/null | grep ';' | cut -d';' -f$field)
	fi

	echo "$iops"
}

#
# Pre-create a test file with sequential writes (buffered, for read tests).
#
function create_read_file
{
	typeset filepath=$1
	# Use /dev/zero — fast, content irrelevant for IOPS benchmarking
	dd if=/dev/zero of="$filepath" bs="$SCHEDULER_BS_HR" \
	    count=$((SCHEDULER_FILESIZE / SCHEDULER_BS)) 2>/dev/null
}

function cleanup
{
	# Pool may already be destroyed during bench phases; ignore errors
	destroy_pool $TESTPOOL 2>/dev/null
	rm -f /var/tmp/sched_test*
}

log_assert "Benchmark all 4 vdev scheduler modes (auto/off/on/wronly)"

log_onexit cleanup

if ! is_linux && ! is_freebsd; then
	log_note "vdev scheduler test requires Linux or FreeBSD"; log_pass
fi

log_note "Using fio ioengine: $IOENGINE"

#
# Destroy and recreate the pool fresh for each scheduler phase,
# so pool aging/fragmentation does not skew IOPS comparisons.
#
function recreate_pool
{
	# Tear down previous pool if it exists
	destroy_pool $TESTPOOL 2>/dev/null
	log_must zpool create -f $TESTPOOL raidz $DISKS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set compression=off $TESTPOOL/$TESTFS
	log_must zfs set recordsize=$SCHEDULER_BS_HR $TESTPOOL/$TESTFS
	log_must zfs set atime=off $TESTPOOL/$TESTFS
	log_must zfs set xattr=sa $TESTPOOL/$TESTFS
}

#
# Set scheduler on all leaf vdevs (uses global $TESTPOOL).
#
function set_all_vdev_scheduler
{
	typeset value=$1
	typeset vdev
	set -A VDEVS $(get_disklist_fullpath $TESTPOOL)
	for vdev in "${VDEVS[@]}"; do
		log_must zpool set scheduler=$value $TESTPOOL $vdev
	done
	log_note "Set scheduler=$value on all ${#VDEVS[*]} vdev(s)"
}

# Verify scheduler property is supported on first vdev
set -A _vdevs $(get_disklist_fullpath $TESTPOOL)
if ! zpool get scheduler $TESTPOOL ${_vdevs[0]} > /dev/null 2>&1; then
	log_note "vdev scheduler property not supported on this platform"
	log_pass
fi

# Destroy the setup pool and reload the zfs module for a pristine kernel state.
# Benchmarks recreate their own pools; this just resets any accumulated
# kernel-internal counters, slab caches, and vdev state from prior tests.
destroy_pool $TESTPOOL 2>/dev/null
if is_linux; then
	if modprobe -r zfs 2>/dev/null; then
		log_must modprobe zfs
		log_note "Reloaded zfs kernel module — clean slate"
	else
		log_note "Could not unload zfs module (in use); continuing anyway"
	fi
elif is_freebsd; then
	if kldunload openzfs 2>/dev/null; then
		log_must kldload openzfs
		log_note "Reloaded zfs kernel module — clean slate"
	else
		log_note "Could not unload zfs module (in use); continuing anyway"
	fi
fi

# Workload definitions
typeset -a workload_names workload_rw workload_needs_file
set -A workload_names  "seq-read"  "seq-write"  "rand-read"  "rand-write"  "rand-rw"
set -A workload_rw     "read"      "write"      "randread"   "randwrite"   "randrw"
set -A workload_file_needed 1 0 1 0 1

# Results: [workload_index] = IOPS value
typeset -a iops_sched_auto iops_sched_off iops_sched_on iops_sched_wronly

#
# Helper: run all 4 workloads under a given scheduler mode, store IOPS.
# Destroys and recreates the pool first to ensure a clean starting state.
#
function bench_scheduler_mode
{
	typeset mode_label=$1    # e.g. "auto"
	typeset mode_value=$2    # e.g. "auto"
	typeset result_array=$3  # name of the result array

	typeset -n results=$result_array

	log_note ""
	log_note "============================================================"
	log_note " Phase: scheduler = $mode_label  (fresh pool)"
	log_note "============================================================"

	recreate_pool
	set_all_vdev_scheduler $mode_value
	log_must zpool sync $TESTPOOL

	typeset mntpnt
	mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

	typeset i=0
	while (( i < ${#workload_names[*]} )); do
		wl_name=${workload_names[$i]}
		wl_rw=${workload_rw[$i]}
		need_file=${workload_file_needed[$i]}
		testfile="$mntpnt/sched_test_${mode_label}_${wl_name}"

		log_note ""
		log_note "--- scheduler=$mode_label  workload=$wl_name  rw=$wl_rw ---"

		if (( need_file )); then
			log_note "Pre-creating test file ($SCHEDULER_FILESIZE_HR)..."
			create_read_file "$testfile"
			log_must zpool sync $TESTPOOL
		fi

		typeset iops_val
		iops_val=$(bench_iops "$mntpnt" "$wl_rw" "$testfile" \
		    "${mode_label}-${wl_name}")
		results[$i]=$iops_val
		log_note "  IOPS (scheduler=$mode_label): $iops_val"

		rm -f "$testfile"
		(( i++ ))
	done
}

# ---- Run benchmarks (wronly first so it gets the freshest host state) ----
bench_scheduler_mode "wronly" "wronly" iops_sched_wronly
bench_scheduler_mode "on"     "on"     iops_sched_on
bench_scheduler_mode "off"    "off"    iops_sched_off
bench_scheduler_mode "auto"   "auto"   iops_sched_auto

# ---- Summary Table (4-way comparison, baseline=auto) ----
log_note ""
log_note "=========================================================================="
log_note " IOPS Comparison Summary (each phase on a fresh pool)"
log_note "   Phase order: wronly → on → off → auto"
log_note "   engine=$IOENGINE  iodepth=$IODEPTH  runtime=${RUNTIME}s  bs=$SCHEDULER_BS_HR"
log_note "   disks: $DISKS"
log_note "=========================================================================="
log_note ""
printf "  %-12s  %8s  %8s  %8s  %8s  %8s  %8s  %8s\n" \
    "WORKLOAD" "AUTO" "OFF" "ON" "WRONLY" "offΔ%" "onΔ%" "wrΔ%"
printf "  %-12s  %8s  %8s  %8s  %8s  %8s  %8s  %8s\n" \
    "------------" "--------" "--------" "--------" "--------" "--------" "--------" "--------"

function calc_pct
{
	typeset new=$1 base=$2
	if [[ -n "$new" && -n "$base" ]] && (( base > 0 )); then
		awk "BEGIN { printf \"%+.1f\", ($new - $base) * 100.0 / $base }"
	else
		echo "N/A"
	fi
}

i=0
while (( i < ${#workload_names[*]} )); do
	wl_name=${workload_names[$i]}
	auto_v=${iops_sched_auto[$i]:-"N/A"}
	off_v=${iops_sched_off[$i]:-"N/A"}
	on_v=${iops_sched_on[$i]:-"N/A"}
	wr_v=${iops_sched_wronly[$i]:-"N/A"}

	off_d=$(calc_pct "$off_v" "$auto_v")
	on_d=$(calc_pct "$on_v" "$auto_v")
	wr_d=$(calc_pct "$wr_v" "$auto_v")

	printf "  %-12s  %8s  %8s  %8s  %8s  %7s%%  %7s%%  %7s%%\n" \
	    "$wl_name" "$auto_v" "$off_v" "$on_v" "$wr_v" "$off_d" "$on_d" "$wr_d"
	(( i++ ))
done

log_note ""
log_note "=========================================================================="
log_note " Scheduler mode behavior:"
log_note "   auto  : Original ZFS default — queue on HDD, bypass on SSD."
log_note "           For loop devices (non-rotational), auto = bypass."
log_note "   off   : Bypasses vdev_queue entirely.  No lock, no tree, no"
log_note "           aggregation.  Best raw IOPS for reads."
log_note "   on    : Full vdev_queue: LBA ordering, aggregation, concurrency"
log_note "           caps.  Write aggregation can merge adjacent I/Os."
log_note "   wronly : Write-only queue — per-I/O-type decision:"
log_note "           - Reads  → bypass (like off, zero queue overhead)"
log_note "           - Writes → queue  (like on,  write aggregation benefit)"
log_note "           - TRIM   → bypass"
log_note ""
log_note "   Ideal result: wronly reads ≈ off reads, wronly writes ≈ on writes."
log_note "=========================================================================="

log_pass "vdev scheduler 4-mode IOPS benchmark completed"
