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
#	Verify pools can be created with the expected feature set enabled.
#
# STRATEGY:
#	1. Create a pool with a known feature set.
#	2. Verify only those features are active/enabled.
#	3. Do this for all known feature sets
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && log_must zpool destroy $TESTPOOL
}

log_onexit cleanup

log_assert "creates a pool with a specified feature set enabled"

for compat in "$ZPOOL_COMPAT_DIR"/*
do
	log_must zpool create -f -o compatibility="${compat##*/}" $TESTPOOL $DISKS
	check_feature_set $TESTPOOL "${compat##*/}"
	log_must zpool destroy -f $TESTPOOL
done

log_pass "creates a pool with a specified feature set enabled"
