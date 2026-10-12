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
# Copyright (c) 2026 by Benjamin Hodgens <ben@hodgens.net>
#

# DESCRIPTION:
# 'zfs events -c' clears a dataset's event log; clearing a dataset
# without an event log fails cleanly with "no event log found".
#
# STRATEGY:
# 1. Enable events=on on a child filesystem, write files, verify
#    records exist.
# 2. Run zfs events -c; verify the ring is empty afterwards.
# 3. On a child with events never enabled, verify zfs events -c fails
#    with the "no event log found" message.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/file_events/file_events.kshlib

function cleanup
{
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-clr"
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-never"
}

log_onexit cleanup

log_assert "'zfs events -c' clears the log; clear without a log fails cleanly"

ds=$(make_fetest_child) || log_fail "create fetest-clr"
log_must zfs set events=on "$ds"
mnt=$(get_prop mountpoint "$ds")

log_must touch "$mnt/clearme-1" "$mnt/clearme-2"
count=$(wait_records "$ds" 2) || log_fail "expected 2 records, got $count"

log_must zfs events -c "$ds"
zpool sync "$TESTPOOL"
count=$(wait_records "$ds" 0)
[[ "$count" -eq 0 ]] ||
    log_fail "after clear, $count records remain"

typeset err="$TMPDIR/file_events_clear.$$"
neverds=$(make_fetest_child) || log_fail "create fetest-never"
log_must zfs rename "$neverds" "$TESTPOOL/$TESTFS/fetest-never"
neverds="$TESTPOOL/$TESTFS/fetest-never"

zfs events -c "$neverds" >"$TMPDIR/file_events_clear.out.$$" 2>"$err"
typeset -i rc=$?
[[ $rc -ne 0 ]] ||
    log_fail "clear on events=off dataset succeeded, expected failure"
grep -q "no event log found" "$err" ||
    log_fail "unexpected error message: $(cat "$err")"
rm -f "$err" "$TMPDIR/file_events_clear.out.$$"

log_pass "'zfs events -c' clears the log; clear without a log fails cleanly"
