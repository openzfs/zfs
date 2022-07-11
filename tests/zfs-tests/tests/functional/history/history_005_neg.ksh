#!/bin/ksh -p
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
