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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
# Copyright (c) 2017 by Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	scrub command fails when there is an existing scrub in progress
#
# STRATEGY:
#	1. Setup a pool and fill it with data
#	2. Kick off a scrub
#	2. Kick off a second scrub and verify it fails
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
}

log_onexit cleanup

log_assert "Scrub command fails when there is already a scrub in progress"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL true
log_mustnot zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL true
log_must zpool scrub -s $TESTPOOL
log_must is_pool_scrub_stopped $TESTPOOL true

log_pass "Issuing a scrub command failed when scrub was already in progress"
