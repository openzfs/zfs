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
# zfs mount returns an error when run as a user
#
# STRATEGY:
#
# 1. Verify that we can't mount the unmounted filesystem created in setup
#
#

log_assert "zfs mount returns an error when run as a user"

log_mustnot zfs mount $TESTPOOL/$TESTFS/$TESTFS2.unmounted

# now verify that the above command didn't do anything
MOUNTED=$(mount | grep $TESTPOOL/$TESTFS/$TESTFS2.unmounted)
if [ -n "$MOUNTED" ]
then
	log_fail "Filesystem $TESTPOOL/$TESTFS/$TESTFS2.unmounted was mounted!"
fi

log_pass "zfs mount returns an error when run as a user"
