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
