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

# Copyright (c) 2025, Klara Inc.
#
# This software was developed by
# Mariusz Zaborski <oshogbo@FreeBSD.org>
# under sponsorship from Wasabi Technology, Inc. and Klara Inc.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_events/zpool_events.kshlib

#
# DESCRIPTION:
#      Verify that using “zpool scrub -C” correctly generates events.
#
# STRATEGY:
#      1. Run an initial “zpool scrub” on the test pool to generate a txg.
#      2. Clear existing pool events.
#      3. Run “zpool scrub -C” to scrub from the last txg.
#      4. Capture the event log and confirm it contains both “scrub_start” and
#         “scrub_finish” entries.
#

verify_runnable "global"

function cleanup
{
	rm -f $EVENTS_FILE
}

EVENTS_FILE="$TESTDIR/zpool_events.$$"
log_onexit cleanup

log_assert "Verify scrub -C events."

# Run an initial “zpool scrub”
log_must zpool scrub -w $TESTPOOL

# Clear existing pool events.
log_must zpool events -c

# Generate new scrub events.
log_must zpool scrub -Cw $TESTPOOL

# Verify events.
log_must eval "zpool events -H > $EVENTS_FILE"
log_must grep "scrub_start" $EVENTS_FILE
log_must grep "scrub_finish" $EVENTS_FILE

log_pass "Verified scrub -C generate correct events."
