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
# zpool upgrade returns an error when run as a user
#
# STRATEGY:
#
# 1. Attempt to upgrade a pool
# 2. Verify the command fails
#

verify_runnable "global"

log_assert "zpool upgrade returns an error when run as a user"

log_onexit cleanup
# zpool upgrade returns 0 when it can't do anything
log_must $ZPOOL upgrade $TESTPOOL.virt

# Now try to upgrade our version 1 pool
log_mustnot $ZPOOL upgrade v1-pool

# if the pool has been upgraded, then v1-pool won't be listed in the output
# of zpool upgrade anymore
RESULT=$($ZPOOL upgrade | $GREP v1-pool)
if [ -z "$RESULT" ]
then
	log_fail "A pool was upgraded successfully!"
fi

log_pass "zpool upgrade returns an error when run as a user"
