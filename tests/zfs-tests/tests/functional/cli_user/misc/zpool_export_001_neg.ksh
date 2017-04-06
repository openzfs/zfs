#!/bin/ksh -p
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zpool export returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to export a pool
# 2. Verify the command fails
#

function check_for_export
{
	RESULT=$(zpool list | grep $TESTPOOL.virt )
	if [ -z "$RESULT" ]
	then
		log_fail "A pool was exported!"
	fi

}

verify_runnable "global"

log_assert "zpool export returns an error when run as a user"

log_mustnot zpool export $TESTPOOL.virt
check_for_export

log_mustnot zpool export -f $TESTPOOL.virt
check_for_export

log_pass "zpool export returns an error when run as a user"
