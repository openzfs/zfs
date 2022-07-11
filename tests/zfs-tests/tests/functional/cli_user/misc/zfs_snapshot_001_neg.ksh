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
# zfs snapshot returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to snapshot a dataset
# 2. Verify the snapshot wasn't taken
#
#

log_assert "zfs snapshot returns an error when run as a user"

log_mustnot zfs snapshot $TESTPOOL/$TESTFS@usersnap1

# Now verify that the above command didn't actually do anything
if datasetexists $TESTPOOL/$TESTFS@usersnap1
then
	log_fail "Snapshot $TESTPOOL/$TESTFS@usersnap1 was taken !"
fi

log_pass "zfs snapshot returns an error when run as a user"
