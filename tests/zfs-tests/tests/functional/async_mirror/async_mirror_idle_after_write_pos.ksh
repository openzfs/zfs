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
#	A pool with the async mirror on goes idle after a write.  A release of
#	records writes a txg, and that txg has a record of its own.  That
#	record must not start the next release, or the pool syncs a txg
#	each time the lagging child copies one, for ever.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Delay each I/O to the lagging child, as for a hard disk.  The
#	   copy of a txg then takes longer than a release.
#	3. Write some files, and wait until the lagging child has caught up.
#	4. With no writes, count the txgs of the pool for 30 seconds.
#	5. Verify that the count is near the rate of an idle pool, one txg
#	   for each txg timeout, and that no txg waits to be copied at
#	   some time.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

typeset -ri secs=30
typeset -ri timeout=$(get_tunable TXG_TIMEOUT)
typeset -ri limit=$(( 3 * (secs / timeout + 1) ))

async_mirror_create_pool
async_mirror_enable

log_must zinject -d $ASYNC_MIRROR_LAGGING -D 20:1 $TESTPOOL
async_mirror_write_files file 4
log_must wait_async_mirror_idle

# Let the releases of the backlog finish.
log_must sleep $(( 2 * timeout ))

typeset -i before=$(get_async_mirror_pool_txg)
typeset -i minpend=$(get_async_mirror_stat pending_txgs)
typeset -i i p

for (( i = 0; i < secs; i++ )); do
	sleep 1
	p=$(get_async_mirror_stat pending_txgs)
	(( p < minpend )) && minpend=$p
done

typeset -i after=$(get_async_mirror_pool_txg)

log_note "$(( after - before )) txgs in $secs seconds, limit $limit," \
    "smallest pending_txgs $minpend"
log_must test $(( after - before )) -le $limit
log_must test $minpend -eq 0
log_must zinject -c all

log_pass "A pool with the async mirror on goes idle after a write."
