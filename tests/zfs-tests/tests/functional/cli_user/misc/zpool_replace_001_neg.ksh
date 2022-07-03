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
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zpool replace returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to replace a device in a pool
# 2. Verify the command fails
#
#

function check_for_replace
{
	sleep 10
	RESULT=$(zpool status -v $TESTPOOL.virt | grep disk-additional.dat)
	if [ -n "$RESULT" ]
	then
		log_fail "A disk was replaced in the pool!"
	fi
}

verify_runnable "global"

log_assert "zpool replace returns an error when run as a user"

log_mustnot zpool replace $TESTPOOL.virt /$TESTDIR/disk-1.dat \
	 /$TESTDIR/disk-additional.dat
check_for_replace

log_mustnot zpool replace -f $TESTPOOL.virt /$TESTDIR/disk-1.dat \
 /$TESTDIR/disk-additional.dat
check_for_replace

log_pass "zpool replace returns an error when run as a user"
