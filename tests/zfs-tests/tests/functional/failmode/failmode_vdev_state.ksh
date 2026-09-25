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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A vdev state change returns when the pool suspends while it
#	waits for its txg to sync.
#
# STRATEGY:
#	1. Create a mirror and stop background txg syncs
#	2. Fail every write and every probe on both sides of the
#	   mirror
#	3. Run zpool online, whose txg sync suspends the pool
#	4. zpool online must return an error within the timeout,
#	   and the pool must be suspended
#

verify_runnable "global"

log_assert "A vdev state change returns when the pool suspends"

typeset -i STATE_TIMEOUT=60
typeset -i zpid=0

#
# Wait up to STATE_TIMEOUT seconds for zpool online to exit.
#
function wait_online
{
	typeset -i waited=0

	while kill -0 $zpid 2>/dev/null; do
		((waited++ >= STATE_TIMEOUT)) && return 1
		sleep 1
	done
	return 0
}

function cleanup
{
	zinject -c all
	zpool clear $TESTPOOL
	if ((zpid > 0)); then
		if wait_online; then
			wait $zpid
		else
			log_note "zpool online is still running"
		fi
	fi
	restore_tunable TXG_TIMEOUT
	destroy_pool $TESTPOOL
	zpool labelclear -f $DISK1
	zpool labelclear -f $DISK2
}

read -r DISK1 DISK2 _ <<<"$DISKS"
if [[ -z "$DISK2" ]]; then
	log_unsupported "this test needs two disks"
fi

log_must save_tunable TXG_TIMEOUT
log_onexit cleanup
log_must set_tunable32 TXG_TIMEOUT 600

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2
log_must zpool sync $TESTPOOL

log_must zinject -d $DISK1 -e io -T write $TESTPOOL
log_must zinject -d $DISK2 -e io -T write $TESTPOOL
log_must zinject -d $DISK1 -e nxio -T probe $TESTPOOL
log_must zinject -d $DISK2 -e nxio -T probe $TESTPOOL
log_must [ "$(kstat_pool $TESTPOOL state)" != "SUSPENDED" ]

zpool online $TESTPOOL $DISK1 &
zpid=$!

if ! wait_online; then
	log_fail "zpool online hung for ${STATE_TIMEOUT}s"
fi
wait $zpid
typeset -i rc=$?
zpid=0

log_note "zpool online returned $rc"
log_must [ "$(kstat_pool $TESTPOOL state)" == "SUSPENDED" ]
log_must [ $rc -ne 0 ]

log_pass "A vdev state change returns when the pool suspends"
