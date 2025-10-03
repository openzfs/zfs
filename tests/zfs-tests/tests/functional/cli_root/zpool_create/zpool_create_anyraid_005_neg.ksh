#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
log_mustnot zpool create $TESTPOOL anymirror4 $disks

log_pass "anyraid vdev specifications detect problems correctly"
