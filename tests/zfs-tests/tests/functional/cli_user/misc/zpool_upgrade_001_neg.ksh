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
# zpool upgrade returns an error when run as a user
#
# STRATEGY:
#
# 1. Attempt to upgrade a pool
# 2. Verify the command fails
#

verify_runnable "global"

log_assert "zpool upgrade returns an error when run as a user"

# zpool upgrade returns 0 when it can't do anything
log_must zpool upgrade $TESTPOOL.virt

# Now try to upgrade our version 1 pool
log_mustnot zpool upgrade v1-pool

# if the pool has been upgraded, then v1-pool won't be listed in the output
# of zpool upgrade anymore
RESULT=$(zpool upgrade | grep v1-pool)
if [ -z "$RESULT" ]
then
	log_fail "A pool was upgraded successfully!"
fi

log_pass "zpool upgrade returns an error when run as a user"
