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

#
# DESCRIPTION:
#	Verify '-o compatibility' reserved values 'off, legacy'
#
# STRATEGY:
#	1. Create a pool with '-o compatibility=off'
#	2. Create a pool with '-o compatibility=legacy'
#	3. Cannot create a pool with '-o compatibility=unknown'
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && log_must zpool destroy $TESTPOOL
}

log_onexit cleanup

log_assert "verify '-o compatibility' reserved values 'off, legacy'"

log_must zpool create -f -o compatibility=off $TESTPOOL $DISKS
log_must zpool destroy -f $TESTPOOL

log_must zpool create -f -o compatibility=legacy $TESTPOOL $DISKS
log_must zpool destroy -f $TESTPOOL

log_mustnot zpool create -f -o compatibility=unknown $TESTPOOL $DISKS

log_pass "verify '-o compatibility' reserved values 'off, legacy'"
