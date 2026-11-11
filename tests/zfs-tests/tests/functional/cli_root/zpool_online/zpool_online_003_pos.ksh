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
# Copyright (c) 2026 by ConnectWise. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_online/zpool_online.cfg

#
# DESCRIPTION:
# An available spare can be offlined and then onlined.
#
# STRATEGY:
# 1. Add $DISK3 as a spare to $TESTPOOL.
# 2. Verify 'zpool offline $TESTPOOL $DISK3' succeeds and the disk is offline.
# 3. Verify 'zpool online $TESTPOOL $DISK3' succeeds and the disk is avail.
#

verify_runnable "global"

function cleanup
{
	log_must zpool remove $TESTPOOL $DISK3
}

log_assert "An available spare can be offlined and onlined"

log_must zpool add $TESTPOOL spare $DISK3
log_onexit cleanup

log_must zpool offline $TESTPOOL $DISK3
log_must check_state $TESTPOOL $DISK3 "offline"

log_must zpool online $TESTPOOL $DISK3
log_must check_state $TESTPOOL $DISK3 "avail"

log_pass "An available spare can be offlined and onlined"
