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
# zfs clone returns an error when run as a user
#
# STRATEGY:
#
# 1. Verify that we're unable to clone snapshots as a user
#
#

log_assert "zfs clone returns an error when run as a user"
log_mustnot zfs clone $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS.myclone

# check to see that the above command really did nothing
if datasetexists $TESTPOOL/$TESTFS.myclone
then
	log_fail "Dataset $TESTPOOL/$TESTFS.myclone should not exist!"
fi
log_pass "zfs clone returns an error when run as a user"
