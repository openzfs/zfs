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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A volume being rewritten while its duplicates are cloned never
#	loses a write.  Every write also goes to a shadow file; after
#	an export and import the volume must match the shadow.  The
#	writer keeps to the lower half of the volume, so every
#	candidate there meets a rewritten block, and every block of
#	the upper half is cloned while the writer dirties the
#	indirect block both halves share.
#

verify_runnable "global"

typeset writer_pid=""

function cleanup
{
	exec 3<&-
	[[ -n $writer_pid ]] && kill $writer_pid 2>/dev/null
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	rm -f $TEST_BASE_DIR/clonedup_shadow \
	    $TEST_BASE_DIR/clonedup_blk $TEST_BASE_DIR/clonedup_stop \
	    $TEST_BASE_DIR/clonedup_pass
	clonedup_cleanup
}

log_assert "concurrent writes to a volume are never lost"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_ENABLED
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset nblk=64
typeset -i nlow=$((nblk / 2))
typeset nbytes=$((CD_BS * nblk))
clonedup_write /$TESTPOOL/a $nblk
log_must zfs create -V $nbytes -b $CD_BS $TESTPOOL/w
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/w
clonedup_zvol_fill /$TESTPOOL/a $TESTPOOL/w
log_must cp /$TESTPOOL/a $TEST_BASE_DIR/clonedup_shadow
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 100
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4
log_must set_tunable32 TXG_TIMEOUT 1

exec 3<> $ZVOL_DEVDIR/$TESTPOOL/w

# Park the run in its apply phase, once the walk has indexed every
# block of w as a candidate.
log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying

rm -f $TEST_BASE_DIR/clonedup_stop $TEST_BASE_DIR/clonedup_pass
(
	typeset i=0
	while [[ ! -f $TEST_BASE_DIR/clonedup_stop ]] &&
	    kill -0 $$ 2>/dev/null; do
		dd if=/dev/urandom of=$TEST_BASE_DIR/clonedup_blk \
		    bs=$CD_BS count=1 status=none
		dd if=$TEST_BASE_DIR/clonedup_blk \
		    of=$ZVOL_DEVDIR/$TESTPOOL/w bs=$CD_BS \
		    seek=$((i % nlow)) conv=notrunc,fsync status=none
		dd if=$TEST_BASE_DIR/clonedup_blk \
		    of=$TEST_BASE_DIR/clonedup_shadow bs=$CD_BS \
		    seek=$((i % nlow)) conv=notrunc status=none
		i=$((i + 1))
		((i == nlow)) && touch $TEST_BASE_DIR/clonedup_pass
		sleep 0.05
	done
) &
writer_pid=$!

# Let the apply go once the writer has rewritten the lower half, so
# no candidate there still holds what the walk saw.
typeset -i t
for ((t = 0; t < 600; t++)); do
	[[ -f $TEST_BASE_DIR/clonedup_pass ]] && break
	sleep 0.1
done
[[ -f $TEST_BASE_DIR/clonedup_pass ]] ||
    log_fail "the writer did not rewrite blocks 0 to $((nlow - 1))"
log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
log_must zpool wait -t clonedup $TESTPOOL
log_must touch $TEST_BASE_DIR/clonedup_stop
wait $writer_pid
writer_pid=""
exec 3<&-
clonedup_sync

# a fresh import drops every cached copy of the volume
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/w
log_must dd if=$ZVOL_DEVDIR/$TESTPOOL/w bs=$CD_BS count=$nblk \
    status=none of=$TEST_BASE_DIR/clonedup_blk
log_must cmp $TEST_BASE_DIR/clonedup_blk \
    $TEST_BASE_DIR/clonedup_shadow
clonedup_jstat_is errors 0
# Every candidate in the lower half met a block the writer had
# rewritten, which shows as a dirty or stale skip.  Nothing writes
# the upper half, so all of it is cloned.
clonedup_jstat_is clonedup_applied $((nblk - nlow))
clonedup_check_shared_vol $TESTPOOL a $TESTPOOL/w \
    "$(clonedup_blocks_from $nlow $nblk)"
typeset dirty=$(clonedup_jstat clonedup_skipped_dirty)
typeset stale=$(clonedup_jstat clonedup_skipped_stale)
log_note "dirty $dirty stale $stale"
clonedup_assert_counts "$dirty" "$stale"
log_must [ $((dirty + stale)) -gt 0 ]
clonedup_leakcheck

log_pass "concurrent writes to a volume are never lost"
