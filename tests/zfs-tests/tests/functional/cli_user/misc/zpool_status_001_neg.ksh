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
# zpool status works when run as a user
#
# STRATEGY:
#
# 1. Run zpool status as a user
# 2. Verify we get output
#

function check_pool_status
{
	RESULT=$(grep "pool:" /tmp/pool-status.$$)
	if [ -z "$RESULT" ]
	then
		log_fail "No pool: string found in zpool status output!"
	fi
	rm /tmp/pool-status.$$
}

verify_runnable "global"

log_assert "zpool status works when run as a user"

log_must eval "zpool status > /tmp/pool-status.$$"
check_pool_status

log_must eval "zpool status -v > /tmp/pool-status.$$"
check_pool_status

log_must eval "zpool status $TESTPOOL> /tmp/pool-status.$$"
check_pool_status

log_must eval "zpool status -v $TESTPOOL > /tmp/pool-status.$$"
check_pool_status

# Make sure -c option works, and that VDEV_PATH and VDEV_UPATH get set.
#
# grep for '^\s+/' to just get the vdevs (not pools).  All vdevs will start with
# a '/' when we specify the path (-P) flag. We check for "{}" to see if one
# of the VDEV variables isn't set.
C1=$(zpool status -P | grep -E '^\s+/' | wc -l)
C2=$(zpool status -P -c 'echo vdev_test{$VDEV_PATH}{$VDEV_UPATH}' | \
    grep -E '^\s+/' | grep -v '{}' | wc -l)

if [ "$C1" != "$C2" ] ; then
	log_fail "zpool status -c option failed.  Expected $C1 vdevs, got $C2"
else
	log_pass "zpool status -c option passed.  Expected $C1 vdevs, got $C2"
fi

# $TESTPOOL.virt has an offline device, so -x will show it
log_must eval "zpool status -x $TESTPOOL.virt > /tmp/pool-status.$$"
check_pool_status

log_pass "zpool status works when run as a user"
