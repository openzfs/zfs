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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib
. $STF_SUITE/tests/functional/cli_root/zpool_condense/zpool_condense.kshlib

verify_runnable "global"

if ! is_condense_debug_available ; then
	log_unsupported "need 'debug' condense type for this test"
fi

function cleanup
{
	poolexists $TESTPOOL && log_must zpool destroy $TESTPOOL
}

log_onexit cleanup

log_assert "Verify zpool wait correctly waits for condense."

log_must zpool create -f $TESTPOOL $DISK1 $DISK2

log_must zpool condense -t debug $TESTPOOL
log_must is_pool_condensing $TESTPOOL debug true

log_must zpool wait -t condense $TESTPOOL

log_must is_pool_condense_done $TESTPOOL debug true

log_must zpool destroy $TESTPOOL

log_assert "Verified zpool wait correctly waits for condense."
