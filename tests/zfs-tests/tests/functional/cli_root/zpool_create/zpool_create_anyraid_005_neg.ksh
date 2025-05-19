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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Run negative tests relating to anyraid vdevs and pool creation
#
# STRATEGY:
# 1. Try to create a pool with an invalid parity string
# 2. Try to create a pool with too large a parity
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "anyraid vdev specifications detect problems correctly"
log_onexit cleanup

create_sparse_files "disk" 4 $MINVDEVSIZE2

log_mustnot zpool create $TESTPOOL anymirrorq $disks
log_mustnot zpool create $TESTPOOL anymirrorq1 $disks
log_mustnot zpool create $TESTPOOL anymirror-1 $disks

#
# vdev names should be reserved so they can't accidentally be used as a pool
# name.
#
log_mustnot zpool create anymirror $disks

log_pass "anyraid vdev specifications detect problems correctly"
