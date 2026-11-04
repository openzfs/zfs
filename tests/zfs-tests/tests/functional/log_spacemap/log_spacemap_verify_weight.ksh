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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# When a metaslab is flushed with ZFS_DEBUG_METASLAB_VERIFY set in
# zfs_flags, metaslab_verify_weight_and_frag() recomputes its weight while
# holding only ms_lock.  Allocations in open context (e.g. ZIL writes) look
# at the active metaslab of their allocator while holding only mg_lock, so
# the verification must not modify ms_weight, or such an allocation can
# trip ASSERT(msp->ms_weight & METASLAB_ACTIVE_MASK) in
# metaslab_group_alloc().
#
# The metaslab_verify_weight_delay_ms tunable makes the verification of
# an active metaslab wait, so that a ZIL write can be issued against the
# log device's active metaslab while it is being verified.
#
# STRATEGY:
#	1. Create a pool with a separate log device and sync=always.
#	2. Write synchronously, so the ZIL activates a log metaslab, and
#	   sync the pool so that metaslab is clean but has unflushed changes.
#	3. Set metaslab_verify_weight_delay_ms and request a flush of all
#	   metaslabs with `zpool condense -t log_spacemap`.
#	4. Once dbgmsg shows the log metaslab's verification has started,
#	   write synchronously again, allocating from that metaslab.
#	5. Verify that the write completes.
#

verify_runnable "global"

LOGSM_POOL="logsm_verify_weight"
DATA_VDEV="$TEST_BASE_DIR/logsm_verify_weight_data"
LOG_VDEV="$TEST_BASE_DIR/logsm_verify_weight_log"
DELAY_MS=500

function cleanup
{
	restore_tunable METASLAB_VERIFY_WEIGHT_DELAY_MS
	restore_tunable ZFS_FLAGS
	if poolexists $LOGSM_POOL; then
		log_must zpool destroy -f $LOGSM_POOL
	fi
	rm -f $DATA_VDEV $LOG_VDEV
}
log_onexit cleanup

#
# Number of delayed verifications of metaslabs of the log device (which is
# top-level vdev 1) seen in the debug log so far.
#
function log_delays
{
	kstat dbgmsg | grep -c \
	    "delaying [0-9]* ms: spa $LOGSM_POOL vdev 1 "
}

log_assert "Verifying a metaslab's weight does not disturb allocations"

log_must save_tunable METASLAB_VERIFY_WEIGHT_DELAY_MS
log_must save_tunable ZFS_FLAGS

# ZFS_DEBUG_METASLAB_VERIFY
typeset flags=$(get_tunable ZFS_FLAGS)
log_must set_tunable32 ZFS_FLAGS $((flags | 0x100))

log_must truncate -s 1G $DATA_VDEV
log_must truncate -s 256M $LOG_VDEV
log_must zpool create -f -o cachefile=none -O sync=always \
    -O compression=off $LOGSM_POOL $DATA_VDEV log $LOG_VDEV

typeset file=/$LOGSM_POOL/file
typeset hit=false
for round in 1 2 3 4 5; do
	log_must dd if=/dev/urandom of=$file bs=4k count=16 conv=notrunc
	sync_pool $LOGSM_POOL
	sync_pool $LOGSM_POOL
	sync_pool $LOGSM_POOL

	typeset before=$(log_delays)
	log_must set_tunable32 METASLAB_VERIFY_WEIGHT_DELAY_MS $DELAY_MS
	log_must zpool condense -t log_spacemap $LOGSM_POOL

	typeset -i i=0
	while ((i < 300)); do
		if (($(log_delays) > before)); then
			hit=true
			break
		fi
		sleep 0.1
		((i++))
	done

	if $hit; then
		# Allocate from the log metaslab while it is being verified.
		dd if=/dev/urandom of=$file bs=4k count=1 conv=notrunc \
		    2>/dev/null &
		typeset pid=$!
		i=0
		while kill -0 $pid 2>/dev/null && ((i < 600)); do
			sleep 0.1
			((i++))
		done
		kill -0 $pid 2>/dev/null && \
		    log_fail "write during weight verification did not complete"
		wait $pid || log_fail "write during weight verification failed"
	fi

	log_must set_tunable32 METASLAB_VERIFY_WEIGHT_DELAY_MS 0
	log_must zpool wait -t condense $LOGSM_POOL
	$hit && break
	log_note "round $round: log metaslab was not verified, retrying"
done

$hit || log_fail "the log device's metaslab was never verified"

log_must zpool scrub -w $LOGSM_POOL
log_must check_pool_status $LOGSM_POOL "errors" "No known data errors"

log_pass "Verifying a metaslab's weight does not disturb allocations"
