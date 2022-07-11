#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2021 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	Verify zpool status only recommends upgrading the pool when
#	the enabled features don't match those in the feature set.
#
# STRATEGY:
#	1. Create a pool with a known feature set.
#	2. Verify there is no `zpool status` notice to upgrade the pool.
#	3. Set the pool compatibility to a newer feature set.
#	4. Verify there is a `zpool status` notice to upgrade the pool.
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL1 && log_must zpool destroy $TESTPOOL1
	rm -f $FILEDEV
}

FILEDEV="$TEST_BASE_DIR/filedev.$$"

log_onexit cleanup

log_assert "check 'zpool status' upgrade notice"

log_must truncate -s $MINVDEVSIZE $FILEDEV
log_must zpool create -f -o compatibility=compat-2018 $TESTPOOL1 $FILEDEV
log_mustnot check_pool_status $TESTPOOL1 "status" "features are not enabled"

log_must zpool set compatibility=compat-2020 $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "status" "features are not enabled"

log_pass "check 'zpool status' upgrade notice"
