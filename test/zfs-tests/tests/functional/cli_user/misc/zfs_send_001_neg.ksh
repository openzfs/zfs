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
# zfs send returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to send a dataset to a file
# 2. Verify the file created has zero-size
#
#

function cleanup
{
	if [ -e /tmp/zfstest_datastream.$$ ]
	then
		log_must $RM /tmp/zfstest_datastream.$$
	fi
}

log_assert "zfs send returns an error when run as a user"
log_onexit cleanup

log_mustnot eval "$ZFS send $TESTPOOL/$TESTFS@snap > /tmp/zfstest_datastream.$$"

# Now check that the above command actually did nothing

# We should have a non-zero-length file in /tmp
if [ -s /tmp/zfstest_datastream.$$ ]
then
	log_fail "A zfs send file was created in /tmp/zfstest_datastream.$$ !"
fi

log_pass "zfs send returns an error when run as a user"
