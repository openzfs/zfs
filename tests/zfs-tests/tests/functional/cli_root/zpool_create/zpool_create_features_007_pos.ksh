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
