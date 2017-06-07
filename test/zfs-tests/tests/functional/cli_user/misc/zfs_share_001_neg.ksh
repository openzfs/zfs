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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
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
# zfs share returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to share a dataset
# 2. Verify the dataset was not shared.
#
#

verify_runnable "global"

log_assert "zfs share returns an error when run as a user"

if is_shared $TESTDIR/unshared
then
	log_fail "$TESTPOOL/$TESTFS/unshared was incorrectly shared initially!"
fi

log_mustnot $ZFS share $TESTPOOL/$TESTFS/unshared

# Now verify that the above command didn't actually do anything
if is_shared $TESTDIR/unshared
then
	log_fail "$TESTPOOL/$TESTFS/unshared was actually shared!"
fi

log_pass "zfs share returns an error when run as a user"
