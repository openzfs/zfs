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
	    log_must zfs destroy -r $TESTPOOL@snap
}

log_assert "'zfs send -R' can send from read-only pools"
log_onexit cleanup

log_must zfs snapshot -r $TESTPOOL@snap

log_must zpool export $TESTPOOL
log_must zpool import -o readonly=on $TESTPOOL

log_must eval "zfs send -R $TESTPOOL@snap >/dev/null"

log_pass "'zfs send -R' can send from read-only pools"
