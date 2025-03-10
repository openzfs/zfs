#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
