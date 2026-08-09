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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
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
# zfs unshare returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to unshare a shared dataset
# 2. Verify the dataset is still shared
#
#

verify_runnable "global"

if is_linux || is_freebsd; then
	log_unsupported "Requires additional dependencies"
fi

log_assert "zfs unshare returns an error when run as a user"

#  verify that the filesystem was shared initially
log_mustnot not_shared $TESTDIR/shared
log_fail "$TESTPOOL/$TESTFS/shared was not shared initially at all!"

log_mustnot zfs unshare $TESTPOOL/$TESTFS/shared

# now verify that the above command didn't do anything
log_mustnot not_shared $TESTDIR/shared
log_fail "$TESTPOOL/$TESTFS/shared was actually unshared!"

log_pass "zfs unshare returns an error when run as a user"
