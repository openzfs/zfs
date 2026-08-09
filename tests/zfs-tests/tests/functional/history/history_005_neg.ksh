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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/history/history_common.kshlib

#
# DESCRIPTION:
#	Verify the following zpool subcommands are not logged.
#		zpool get
#		zpool history
#		zpool list
#		zpool status
#		zpool iostat
#
# STRATEGY:
#	1. Create a test pool
#	2. Separately invoke zpool list|status|iostat
#	3. Verify they were not recorded in pool history
#

verify_runnable "global"

log_assert "Verify 'zpool get|history|list|status|iostat' will not be logged."

# Save initial TESTPOOL history
log_must eval "zpool history $TESTPOOL >$OLD_HISTORY"

log_must eval "zpool get all $TESTPOOL >/dev/null"
log_must eval "zpool list $TESTPOOL >/dev/null"
log_must eval "zpool status $TESTPOOL >/dev/null"
log_must eval "zpool iostat $TESTPOOL >/dev/null"

log_must eval "zpool history $TESTPOOL >$NEW_HISTORY"
log_must diff $OLD_HISTORY $NEW_HISTORY

log_pass "Verify 'zpool get|history|list|status|iostat' will not be logged."
