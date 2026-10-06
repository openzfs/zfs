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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_destroy/zpool_destroy.cfg

#
# DESCRIPTION:
# Verify that a healthy pool without mountpoints can be destroyed using
# hardforce flag.
#
# STRATEGY:
# 1. Create a pool.
# 2. Unmount the fs.
# 3. Forcibly destroy.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "zpool destroy -F of a healthy pool without mountpoints."
log_onexit cleanup

log_must create_pool $TESTPOOL raidz $FEDISK0 $FEDISK1 $FEDISK2
log_must zfs unmount /$TESTPOOL
log_must zpool destroy -F $TESTPOOL
log_mustnot poolexists $TESTPOOL

log_pass "zpool destroy -F of a healthy pool."
