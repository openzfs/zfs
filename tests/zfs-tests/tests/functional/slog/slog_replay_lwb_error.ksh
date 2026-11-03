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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify that an fsync(2) that returns after an earlier log block
#	failed survives a crash.
#
# STRATEGY:
#	1. Create a pool with a slog, and slow down writes to the main
#	   vdevs so that a txg sync takes many seconds.
#	2. Make writes to the slog fail without doing anything
#	   (io-prefail).  Write file A and fsync it: its log block fails,
#	   so the fsync falls back to waiting for the txg to sync.
#	3. Let slog writes succeed again.  Write file B and fsync it.
#	   B's log block comes after A's in the log chain.
#	4. "Crash": make every write to the pool fail without doing
#	   anything, then copy the vdevs.  The txg holding A has not
#	   synced, so in the copy, the log ends before A's log block.
#	5. Import the copy.  If B's fsync had returned before the crash,
#	   B must be there.
#

verify_runnable "global"

function cleanup_lwb_error
{
	zinject -c all >/dev/null 2>&1
	poolexists $TESTPOOL && zpool clear $TESTPOOL >/dev/null 2>&1
	wait
	cleanup
	rm -f $SRC_A $SRC_B $B_DONE
}

typeset SRC_A=$TEST_BASE_DIR/lwb_error_a.$$
typeset SRC_B=$TEST_BASE_DIR/lwb_error_b.$$
typeset B_DONE=$TEST_BASE_DIR/lwb_error_b_done.$$

log_assert "An fsync that returns after an earlier log block failed" \
    "survives a crash."
log_onexit cleanup_lwb_error
log_must setup

log_must zpool create $TESTPOOL $VDEV log $SDEV
log_must zfs create -o compression=off -o recordsize=128k \
    $TESTPOOL/$TESTFS
typeset fs=/$TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=$SRC_A bs=128k count=1
log_must dd if=/dev/urandom of=$SRC_B bs=128k count=1

# Make sure the dataset has a ZIL header before slowing anything down.
log_must dd if=/dev/zero of=$fs/sync conv=fsync bs=128k count=1

# A txg sync now takes many 3 s rounds of writes.
for dev in $VDEV; do
	log_must eval "zinject -d $dev -D 3000:64 -T write $TESTPOOL >/dev/null"
done

# A's log block fails; its fsync waits for the txg to sync.
typeset errs
errs=$(kstat zil.zil_commit_error_count)
handler=$(zinject -d $SDEV -e io-prefail -T write $TESTPOOL | \
    sed -n 's/^Added handler \([0-9]*\).*/\1/p')
[[ -n $handler ]] || log_fail "zinject -e io-prefail failed"
dd if=$SRC_A of=$fs/a bs=128k count=1 conv=fsync 2>/dev/null &
typeset -i i
for ((i = 0; i < 50; i++)); do
	(( $(kstat zil.zil_commit_error_count) > errs )) && break
	sleep 0.1
done
log_must test "$(kstat zil.zil_commit_error_count)" -gt "$errs"
log_must eval "zinject -c $handler >/dev/null"

# B's log block is written; record if its fsync returns.
( dd if=$SRC_B of=$fs/b bs=128k count=1 conv=fsync 2>/dev/null && \
    touch $B_DONE ) &
sleep 1

# Crash: nothing more reaches the vdevs.
typeset b_acked=no
[[ -f $B_DONE ]] && b_acked=yes
for dev in $VDEV $SDEV; do
	log_must eval "zinject -d $dev -e io-prefail -T write $TESTPOOL >/dev/null"
done
log_note "fsync of B returned before the crash: $b_acked"
log_must cp $VDEV $SDEV $VDIR2/

log_must eval "zinject -c all >/dev/null"
log_must zpool clear $TESTPOOL
wait
log_must zpool destroy -f $TESTPOOL

log_must zpool import -d $VDIR2 $TESTPOOL
if [[ $b_acked == yes ]]; then
	log_must cmp $SRC_B $fs/b
fi

log_pass "An fsync that returns after an earlier log block failed" \
    "survives a crash."
