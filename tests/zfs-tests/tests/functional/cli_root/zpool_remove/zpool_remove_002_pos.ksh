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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_remove/zpool_remove.cfg

#
# DESCRIPTION:
# Verify that 'zpool can only remove inactive hot spare devices from pool'
#
# STRATEGY:
# 1. Create a hotspare pool
# 2. Try to remove the inactive hotspare device from the pool
# 3. Verify that the remove succeed.
#

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
}

log_onexit cleanup

typeset spare_devs1="${DISK0}"
typeset spare_devs2="${DISK1}"

log_assert "zpool remove can only remove inactive hotspare device from pool"

log_note "check hotspare device which is created by zpool create"
log_must zpool create $TESTPOOL $spare_devs1 spare $spare_devs2
log_must zpool remove $TESTPOOL $spare_devs2

log_note "check hotspare device which is created by zpool add"
log_must zpool add $TESTPOOL spare $spare_devs2
log_must zpool remove $TESTPOOL $spare_devs2
log_must zpool destroy $TESTPOOL

log_pass "zpool remove can only remove inactive hotspare device from pool"
