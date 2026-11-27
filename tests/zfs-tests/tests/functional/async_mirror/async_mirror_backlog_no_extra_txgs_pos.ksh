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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	The copy of a backlog to the lagging child adds no txgs of its own.
#	Each txg writes an uberblock, and the ring of uberblocks is small.
#	A release that forced a txg for each copied txg made a fast copy
#	sync tens of txgs a second, and pushed the rewind points out of the
#	ring.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   a file, wait until the lagging child has caught up, and let the
#	   releases end.
#	2. Stop the copy, and write one small file in each of 20 txgs.
#	3. Note the newest txg in the txgs kstat, and restart the copy.
#	   Wait until the lagging child has caught up, and then wait for the
#	   last release to run.
#	4. Verify that the pool does not sync a txg for each copied txg.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

typeset -ri ntxgs=20
typeset -ri timeout=$(get_tunable TXG_TIMEOUT)

#
# Print the newest txg that the txgs kstat shows as committed.
#
function newest_committed_txg
{
	kstat_pool $TESTPOOL txgs | awk '$3 == "C" { t = $1 } END { print t }'
}

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 1
log_must wait_async_mirror_idle
log_must sleep $(( 2 * timeout ))

async_mirror_suspend_copy 1
typeset -i i

for (( i = 0; i < ntxgs; i++ )); do
	log_must file_write -o create -f /$TESTPOOL/b.$i -b 8192 -c 1 -d R
	sync_pool $TESTPOOL
done
log_must test "$(get_async_mirror_stat pending_txgs)" -ge $ntxgs

typeset -i before=$(newest_committed_txg)
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle
log_must sleep $(( timeout + 1 ))
typeset -i after=$(newest_committed_txg)

log_note "$(( after - before )) txgs while $ntxgs txgs were copied"
log_must test $(( after - before )) -le 3

log_pass "The copy of a backlog adds no txgs of its own."
