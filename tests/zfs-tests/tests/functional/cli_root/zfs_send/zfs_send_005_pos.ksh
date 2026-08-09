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

#
# DESCRIPTION:
# 'zfs send -R' can send from read-only imported pool. It needs to
# detect that the pool is read-only and not try to place holds on
# datasets being sent.
#
# STRATEGY:
# 1. Create a recursive snapshot on the whole pool.
# 2. 'zfs send -R' the recursive snapshots.
#

verify_runnable "both"

function cleanup
{
	poolexists $TESTPOOL && log_must_busy zpool export $TESTPOOL
	log_must zpool import $TESTPOOL

	datasetexists $TESTPOOL@snap && \
		destroy_dataset $TESTPOOL@snap -r
}

log_assert "'zfs send -R' can send from read-only pools"
log_onexit cleanup

log_must zfs snapshot -r $TESTPOOL@snap

log_must zpool export $TESTPOOL
log_must zpool import -o readonly=on $TESTPOOL

log_must eval "zfs send -R $TESTPOOL@snap > /dev/null"

log_pass "'zfs send -R' can send from read-only pools"
