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
