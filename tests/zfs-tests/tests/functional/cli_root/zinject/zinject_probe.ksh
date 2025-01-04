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
