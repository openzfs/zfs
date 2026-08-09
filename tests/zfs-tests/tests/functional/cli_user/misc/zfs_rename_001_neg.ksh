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
# zfs rename returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to rename a dataset
# 2. Verify that the renamed dataset does not exist.
#
#

log_assert "zfs rename returns an error when run as a user"

log_mustnot zfs rename $TESTPOOL/$TESTFS/renameme $TESTPOOL/$TESTFS/renameme1

# now verify the above command didn't actually do anything
if datasetexists $TESTPOOL/$TESTFS/renameme1
then
	log_fail "The dataset $TESTPOOL/$TESTFS/renameme was renamed!"
fi

log_pass "zfs rename returns an error when run as a user"
