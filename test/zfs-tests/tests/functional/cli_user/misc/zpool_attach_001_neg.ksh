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
# zpool attach returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to attach a disk to a pool
# 2.Verify that the attach failed
#
#

function check_for_attach
{
	RESULT=$($ZPOOL status -v $TESTPOOL.virt | $GREP disk-additional.dat)
	if [ -n "$RESULT" ]
	then
		log_fail "A disk was attached to the pool!"
	fi
}

verify_runnable "global"

log_assert "zpool attach returns an error when run as a user"

log_mustnot $ZPOOL attach $TESTPOOL.virt /$TESTDIR/disk1.dat \
	/$TESTDIR/disk-additional.dat
check_for_attach

log_mustnot $ZPOOL attach -f $TESTPOOL.virt /$TESTDIR/disk1.dat \
	 /$TESTDIR/disk-additional.dat
check_for_attach

log_pass "zpool attach returns an error when run as a user"
