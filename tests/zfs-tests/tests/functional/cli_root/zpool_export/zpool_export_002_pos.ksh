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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# The 'zpool export' command must fail when a pool is
# busy i.e. mounted.
#
# STRATEGY:
# 1. Try and export the default pool when mounted and busy.
# 2. Verify an error is returned.
#

verify_runnable "global"

function cleanup
{
	cd $olddir || \
	    log_fail "Couldn't cd back to $olddir"

	zpool_export_cleanup
}

olddir=$PWD

log_onexit cleanup

log_assert "Verify a busy ZPOOL cannot be exported."

ismounted "$TESTPOOL/$TESTFS"
(( $? != 0 )) && \
    log_fail "$TESTDIR not mounted. Unable to continue."

cd $TESTDIR || \
    log_fail "Couldn't cd to $TESTDIR"

log_mustnot zpool export $TESTPOOL

poolexists $TESTPOOL || \
	log_fail "$TESTPOOL not found in 'zpool list' output."

log_pass "Unable to export a busy ZPOOL as expected."
