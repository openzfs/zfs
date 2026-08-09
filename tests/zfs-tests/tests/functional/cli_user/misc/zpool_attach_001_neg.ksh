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
# zpool attach returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to attach a disk to a pool
# 2.Verify that the attach failed
#
#

function check_for_attach
{
	RESULT=$(zpool status -v $TESTPOOL.virt | grep disk-additional.dat)
	if [ -n "$RESULT" ]
	then
		log_fail "A disk was attached to the pool!"
	fi
}

verify_runnable "global"

log_assert "zpool attach returns an error when run as a user"

log_mustnot zpool attach $TESTPOOL.virt /$TESTDIR/disk1.dat \
	/$TESTDIR/disk-additional.dat
check_for_attach

log_mustnot zpool attach -f $TESTPOOL.virt /$TESTDIR/disk1.dat \
	 /$TESTDIR/disk-additional.dat
check_for_attach

log_pass "zpool attach returns an error when run as a user"
