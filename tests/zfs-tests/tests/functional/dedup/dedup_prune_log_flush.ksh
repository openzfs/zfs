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
#	Pruning does not remove a dedup table entry whose second reference is
#	flushed from the log after the prune walk has read the entry.
#
# STRATEGY:
#	1. In a userspace pool, store a dedup entry with one reference, and
#	   add a second reference that is still in the log when the prune
#	   walk reads the entry.
#	2. Prune, with the entry in the prune's second batch, so that the
#	   log is flushed between the walk and the entry's removal.
#	3. Require the entry to survive, and zdb to find no block that is
#	   referenced but free after its second reference is removed.
#

verify_runnable "global"

function cleanup
{
	rm -rf $workdir
}

log_assert "Pruning keeps a dedup entry whose references arrive by log flush"
workdir=$(mktemp -d $TEST_BASE_DIR/dedup_prune_log_flush.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must ddt_prune_probe $workdir
log_must grep "pruned .* entries .* across .* txg syncs" $workdir/dbgmsg
# Two batches put the entry after the log flush; one more entry pruned
# would be the entry itself.
log_must grep -q "pruned 1 entries .* across 2 txg syncs" $workdir/dbgmsg
log_must zdb -e -p $workdir -bcc ddt_prune_probe

log_pass "Pruning kept a dedup entry whose references arrived by log flush"
