#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
