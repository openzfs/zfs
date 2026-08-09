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
	log_must cd $olddir
	zpool_export_cleanup
}

olddir=$PWD

log_onexit cleanup

log_assert "Verify a busy ZPOOL cannot be exported."

log_must ismounted "$TESTPOOL/$TESTFS"
log_must cd $TESTDIR
log_mustnot zpool export $TESTPOOL
log_must poolexists $TESTPOOL

log_pass "Unable to export a busy ZPOOL as expected."
