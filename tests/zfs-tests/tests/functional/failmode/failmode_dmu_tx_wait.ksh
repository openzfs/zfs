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

log_assert "dmu_tx_assign() blocks when pool suspends with failmode=wait"

typeset -i dd_pid=0

function cleanup
{
	zinject -c all || true
	zpool clear $TESTPOOL || true
	test $dd_pid -gt 0 && kill -9 $dd_pid || true
	destroy_pool $TESTPOOL
}

log_onexit cleanup

DISK=${DISKS%% *}

# create a single-disk pool, set failmode=wait
log_must zpool create -o failmode=wait -f $TESTPOOL $DISK
log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS

# start writing to a file in the background. these args to dd will make it
# keep writing until it fills the pool, but we will kill it before that happens.
dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/file bs=128k &
dd_pid=$!

# give it a moment for the write throttle to start pushing back
sleep 2

# force the pool to suspend by inducing the writes and followup probe to fail
log_must zinject -d $DISK -e io -T write $TESTPOOL
log_must zinject -d $DISK -e nxio -T probe $TESTPOOL

# should only take a moment, but give it a chance
log_note "waiting for pool to suspend"
typeset -i tries=10
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool didn't suspend"
	fi
	sleep 1
done

# dmu_tx_try_assign() should have noticed the suspend by now
typeset -i suspended=$(kstat dmu_tx.dmu_tx_suspended)

# dd should still be running, blocked in the kernel
typeset -i blocked
if kill -0 $dd_pid ; then
	blocked=1
	log_note "dd is blocked as expected"
else
	blocked=0
	log_note "dd exited while pool suspended!"
fi

# bring the pool back online
log_must zinject -c all
log_must zpool clear $TESTPOOL

# kill dd, we're done with it
kill -9 $dd_pid
wait $dd_pid
dd_pid=0

# confirm that dd was blocked in dmu_tx assign/wait
log_must test $suspended -ne 0
log_must test $blocked -eq 1

log_pass "dmu_tx_assign() blocks when pool suspends with failmode=wait"
