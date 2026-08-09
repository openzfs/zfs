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
