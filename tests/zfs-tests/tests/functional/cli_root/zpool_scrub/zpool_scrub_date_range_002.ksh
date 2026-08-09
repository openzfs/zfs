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
# Copyright 2025 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#       Verify that the timestamp database updates all the tables as expected.
#
# STRATEGY:
#     1. Decrease the note and flush frequency of the txg database.
#     2. Force the pool to sync several txgs
#     3. Verify that there are entries in each of the "month", "day", and
#        "minute" tables.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable SPA_NOTE_TXG_TIME
	log_must restore_tunable SPA_FLUSH_TXG_TIME
	rm /$TESTPOOL/f1
}

log_onexit cleanup

log_assert "Verifiy timestamp databases all update as expected."

log_must save_tunable SPA_NOTE_TXG_TIME
log_must set_tunable64 SPA_NOTE_TXG_TIME 1
log_must save_tunable SPA_FLUSH_TXG_TIME
log_must set_tunable64 SPA_FLUSH_TXG_TIME 1

log_must touch /$TESTPOOL/f1
log_must zpool sync $TESTPOOL
sleep 1
log_must touch /$TESTPOOL/f1
log_must zpool sync $TESTPOOL
sleep 1
log_must touch /$TESTPOOL/f1
log_must zpool sync $TESTPOOL

mos_zap="$(zdb -dddd $TESTPOOL 1)"
minutes_entries=$(echo "$mos_zap" | grep "txg_log_time:minutes" | awk '{print $5}')
days_entries=$(echo "$mos_zap" | grep "txg_log_time:days" | awk '{print $5}')
months_entries=$(echo "$mos_zap" | grep "txg_log_time:months" | awk '{print $5}')

[[ "$minutes_entries" -ne "0" ]] || log_fail "0 entries in the minutes table"
[[ "$days_entries" -ne "0" ]] || log_fail "0 entries in the days table"
[[ "$months_entries" -ne "0" ]] || log_fail "0 entries in the months table"

log_pass "Verified all timestamp databases had entries as expected."
