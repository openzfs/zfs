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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zpool status works when run as a user
#
# STRATEGY:
#
# 1. Run zpool status as a user
# 2. Verify we get output
#

function check_pool_status
{
	RESULT=$(grep "pool:" $TEST_BASE_DIR/pool-status.$$)
	if [ -z "$RESULT" ]
	then
		log_fail "No pool: string found in zpool status output!"
	fi
	rm $TEST_BASE_DIR/pool-status.$$
}

verify_runnable "global"

log_assert "zpool status works when run as a user"

log_must eval "zpool status > $TEST_BASE_DIR/pool-status.$$"
check_pool_status

log_must eval "zpool status -v > $TEST_BASE_DIR/pool-status.$$"
check_pool_status

log_must eval "zpool status $TESTPOOL> $TEST_BASE_DIR/pool-status.$$"
check_pool_status

log_must eval "zpool status -v $TESTPOOL > $TEST_BASE_DIR/pool-status.$$"
check_pool_status

log_pass "zpool status works when run as a user"
