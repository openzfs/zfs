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
#	After a rewind import below the newest txg of the lagging child, an
#	import of the lagging child alone does not select a txg of the
#	discarded history, even when the pool was exported before any txg
#	was copied to the child.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   keep the txg of the pool.
#	3. Write files b, wait until the lagging child has caught up, and
#	   export the pool.
#	4. Stop the copy.  Import the pool with -T at the kept txg, and
#	   export it at once.
#	5. Hide the leader, and import the lagging child alone, read-only.
#	6. Verify that the import fails, or selects a txg that is not in
#	   the discarded history.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 4
async_mirror_suspend_release 1
log_must wait_async_mirror_idle
typeset -i txg=$(get_async_mirror_pool_txg)

async_mirror_write_files b 4
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

typeset -i newest=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "rewind txg $txg, newest txg of the lagging child $newest"
log_must test $newest -gt $txg

async_mirror_suspend_copy 1
async_mirror_import_txg $txg
async_mirror_suspend_release 0
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
if zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL; then
	typeset -i alone=$(get_async_mirror_pool_txg)
	log_note "the lagging child alone is at txg $alone"
	if (( alone > txg && alone <= newest )); then
		log_fail "The lagging child alone selected the discarded" \
		    "txg $alone"
	fi
else
	log_note "The lagging child alone cannot be imported"
fi

log_pass "The lagging child alone does not select a discarded txg."
