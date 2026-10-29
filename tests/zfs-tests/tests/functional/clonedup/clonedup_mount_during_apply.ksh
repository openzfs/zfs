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
#	Mounting and unmounting a filesystem while its files are
#	being rewritten never fails: the apply thread steps aside when
#	asked.
#

verify_runnable "global"

typeset loop_pid=""
typeset errs=$TEST_BASE_DIR/clonedup_mount_errs
typeset stop=$TEST_BASE_DIR/clonedup_stop

function cleanup
{
	[[ -n $loop_pid ]] && kill $loop_pid 2>/dev/null
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	rm -f $errs $stop $TEST_BASE_DIR/clonedup_keep
	clonedup_cleanup
}

log_assert "mount and unmount succeed while the apply runs"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset nblk=64
log_must zfs create $TESTPOOL/um
clonedup_write /$TESTPOOL/um/a $nblk
clonedup_dup /$TESTPOOL/um/a /$TESTPOOL/um/b
log_must cp /$TESTPOOL/um/a $TEST_BASE_DIR/clonedup_keep
clonedup_sync
log_must zfs unmount $TESTPOOL/um

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 100
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4
log_must set_tunable32 TXG_TIMEOUT 1

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying

rm -f $errs $stop
(
	while [[ ! -f $stop ]] && kill -0 $$ 2>/dev/null; do
		zfs mount $TESTPOOL/um 2>>$errs || \
		    echo "mount $?" >>$errs
		sleep 0.2
		zfs unmount $TESTPOOL/um 2>>$errs || \
		    echo "unmount $?" >>$errs
		sleep 0.2
	done
) &
loop_pid=$!

log_must zpool wait -t clonedup $TESTPOOL
log_must touch $stop
wait $loop_pid
loop_pid=""

log_note "mount loop errors: $(cat $errs 2>/dev/null)"
log_must [ ! -s $errs ]
clonedup_jstat_gt clonedup_applied 0
clonedup_jstat_is errors 0
# The operation found the apply holding datasets and made it let
# go, which is the path under test; without that it proved nothing.
clonedup_kstat_gt yields 0

ismounted $TESTPOOL/um || log_must zfs mount $TESTPOOL/um
log_must cmp /$TESTPOOL/um/a $TEST_BASE_DIR/clonedup_keep
log_must cmp /$TESTPOOL/um/b $TEST_BASE_DIR/clonedup_keep
log_must rm -f $TEST_BASE_DIR/clonedup_keep
clonedup_leakcheck

log_pass "mount and unmount succeed while the apply runs"
