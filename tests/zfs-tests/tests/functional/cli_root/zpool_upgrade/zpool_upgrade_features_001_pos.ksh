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
# Copyright (c) 2021 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	Verify pools can be upgraded to known feature sets.
#
# STRATEGY:
#	1. Create a pool with a known feature set.
#	2. Verify only those features are active/enabled.
#	3. Upgrade the pool to a newer feature set.
#	4. Verify only those features are active/enabled.
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL1 && log_must zpool destroy $TESTPOOL1
	rm -f $FILEDEV
}

FILEDEV="$TEST_BASE_DIR/filedev.$$"

log_onexit cleanup

log_assert "verify pools can be upgraded to known feature sets."

log_must truncate -s $MINVDEVSIZE $FILEDEV
log_must zpool create -f -o compatibility=compat-2018 $TESTPOOL1 $FILEDEV
check_feature_set $TESTPOOL1 compat-2018
log_mustnot check_pool_status $TESTPOOL1 "status" "features are not enabled"

log_must zpool set compatibility=compat-2020 $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "status" "features are not enabled"

log_must zpool upgrade $TESTPOOL1
check_feature_set $TESTPOOL1 compat-2020
log_mustnot check_pool_status $TESTPOOL1 "status" "features are not enabled"

log_pass "verify pools can be upgraded to known feature sets."
