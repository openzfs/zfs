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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A percentage prune whose target rounds down to no entries prunes none.
#
# STRATEGY:
#	1. Create a pool with three unique dedup entries.
#	2. Verify ddtprune -p 1 keeps all of them.
#	3. Verify ddtprune -p 100 removes all of them.
#

verify_runnable "both"

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

function ddt_entries
{
	zpool status -D $TESTPOOL | awk '
	    /dedup: no DDT entries/ { print 0 }
	    /dedup: DDT entries/ { sub(",", "", $4); print $4 }'
}

log_assert "A percentage prune of less than one entry prunes none"

log_must save_tunable DEDUP_LOG_TXG_MAX
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_onexit cleanup
# Flush the dedup log every txg, so entries are in the DDT ZAP to prune.
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000

log_must zpool create -f $TESTPOOL $DISKS
log_must zfs create -o recordsize=512 -o compression=off -o dedup=on \
    $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
for fill in 65 66 67; do
	log_must file_write -o create -f $mountpoint/$fill -b 512 -c 1 -d $fill
done
sync_pool $TESTPOOL
# Pruning takes entries older than the current second.
sleep 1

typeset -i entries=$(ddt_entries)
(( entries == 3 )) || log_fail "expected 3 DDT entries, found $entries"

log_must zpool ddtprune -p 1 $TESTPOOL
sync_pool $TESTPOOL
entries=$(ddt_entries)
(( entries == 3 )) || log_fail "1% pruned $((3 - entries)) of 3 entries"

log_must zpool ddtprune -p 100 $TESTPOOL
sync_pool $TESTPOOL
entries=$(ddt_entries)
(( entries == 0 )) || log_fail "100% left $entries of 3 entries"

log_must zdb -b $TESTPOOL

log_pass "A percentage prune of less than one entry prunes none"
