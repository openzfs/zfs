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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "Check zinject can correctly inject a probe failure."

DISK1=${DISKS%% *}

function cleanup
{
	log_pos zinject -c all
	log_pos zpool clear $TESTPOOL
	log_pos zpool destroy -f $TESTPOOL
	log_pos restore_tunable TXG_TIMEOUT
}

log_onexit cleanup

log_must zpool create $TESTPOOL $DISK1

# set the txg timeout a long way out, to try and avoid the pool syncing
# between error injection and writing
save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 600

# force a sync now
log_must zpool sync -f

# write stuff. this should go into memory, not written yet
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=1

# inject faults
log_must zinject -d $DISK1 -e io -T probe $TESTPOOL
log_must zinject -d $DISK1 -e io -T write $TESTPOOL

# force the sync now. backgrounded, because the pool will suspend and we don't
# want to block.
log_pos zpool sync &

log_note "waiting for pool to suspend"
typeset -i tries=30
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool didn't suspend"
	fi
	sleep 1
done

log_pass "zinject can correctly inject a probe failure."
