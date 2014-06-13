#!/usr/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg
. $STF_SUITE/include/libtest.shlib

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
	RESULT=$($GREP "pool:" /tmp/pool-status.$$)
	if [ -z "$RESULT" ]
	then
		log_fail "No pool: string found in zpool status output!"
	fi
	$RM /tmp/pool-status.$$
}

verify_runnable "global"

log_assert "zpool status works when run as a user"

log_must eval "$ZPOOL status > /tmp/pool-status.$$"
check_pool_status

log_must eval "$ZPOOL status -v > /tmp/pool-status.$$"
check_pool_status

log_must eval "$ZPOOL status $TESTPOOL> /tmp/pool-status.$$"
check_pool_status

log_must eval "$ZPOOL status -v $TESTPOOL > /tmp/pool-status.$$"
check_pool_status

# $TESTPOOL.virt has an offline device, so -x will show it
log_must eval "$ZPOOL status -x $TESTPOOL.virt > /tmp/pool-status.$$"
check_pool_status

log_pass "zpool status works when run as a user"
