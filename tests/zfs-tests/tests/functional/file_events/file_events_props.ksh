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
# events property behavior: inheritance, events_size validation,
# events_io dependency on events, and default-off silence.
#
# STRATEGY:
# 1. Enable events=on on a parent and verify a child inherits it.
# 2. Set events_size=128K and verify the readback; verify a
#    non-numeric events_size is rejected.
# 3. Verify events_io=on is refused while events=off, with the
#    "must be enabled" message.
# 4. Verify a default events=off dataset records nothing after writes.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/file_events/file_events.kshlib

function cleanup
{
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-p"
}

log_onexit cleanup

log_assert "events property: inheritance, events_size validation, events_io gating"

typeset ds="$TESTPOOL/$TESTFS/fetest-p"
log_must zfs create "$ds"
log_must zfs set events=on "$ds"

typeset child="$ds/child"
log_must zfs create "$child"
typeset val=$(zfs get -H -o value events "$child")
[[ "$val" == "on" ]] ||
    log_fail "child inherited events=$val, expected on"

log_must zfs set events_size=128K "$ds"
val=$(zfs get -H -o value events_size "$ds")
[[ "$val" == "128K" ]] ||
    log_fail "events_size readback $val, expected 128K"

log_mustnot zfs set events_size=notanumber "$ds"

# events_io requires events=on
log_must zfs set events=off "$ds"
log_mustnot_expect "must be enabled" zfs set events_io=on "$ds"

# default events=off: writes produce no records
log_must touch "$(get_prop mountpoint "$ds")/plain"
count=$(wait_records "$ds" 0)
[[ "$count" -eq 0 ]] ||
    log_fail "events=off dataset recorded $count records"

log_pass "events property: inheritance, events_size validation, events_io gating"
