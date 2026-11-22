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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	TRIM and initialize do not touch the lagging child.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write some data.
#	3. Verify that 'zpool trim' of the lagging child fails.
#	4. Verify that 'zpool initialize' of the lagging child fails.
#	5. Verify that 'zpool initialize' of the leader works.
#	6. Verify that the lagging child is not initialized.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files file 2

log_mustnot zpool trim $TESTPOOL $ASYNC_MIRROR_LAGGING
log_mustnot zpool initialize $TESTPOOL $ASYNC_MIRROR_LAGGING

log_must zpool initialize -w $TESTPOOL $ASYNC_MIRROR_LEADER
log_mustnot eval "zpool status -i $TESTPOOL | grep $ASYNC_MIRROR_LAGGING |
    grep -q completed"

log_pass "TRIM and initialize leave the lagging child alone."
