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
. $STF_SUITE/tests/functional/cli_root/zpool_events/zpool_events.kshlib

#
# DESCRIPTION:
# Event cursors belong to open file descriptions.
#
# STRATEGY:
# 1. Generate at least two events.
# 2. Verify independent opens read the same first event.
# 3. Verify duplicates share cursor reads and seeks, including after close.
#

verify_runnable "both"

log_assert "Event cursors are independent across opens and shared by duplicates"

log_must zpool events -c
log_must zpool clear $TESTPOOL
log_must zpool clear $TESTPOOL
zpool_events_settle 2
log_must events_fd

log_pass "Event cursor ownership follows the open file description"
