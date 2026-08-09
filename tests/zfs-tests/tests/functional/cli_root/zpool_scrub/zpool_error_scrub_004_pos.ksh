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
# Copyright (c) 2023, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify error scrub clears the errorlog, if errors no longer exist.
#
# STRATEGY:
#	1. Create a pool with head_errlog disabled.
#	2. Run an error scrub and verify it is not supported.
#

verify_runnable "global"

function cleanup
{
	rm -f /$TESTPOOL2/$TESTFILE0
	destroy_pool $TESTPOOL2
}

log_onexit cleanup

log_assert "Verify error scrub cannot run without the head_errlog feature."

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -o feature@head_errlog=disabled $TESTPOOL2 $TESTDIR/vdev_a
log_mustnot zpool scrub -ew $TESTPOOL2

log_pass "Verify error scrub cannot run without the head_errlog feature."

