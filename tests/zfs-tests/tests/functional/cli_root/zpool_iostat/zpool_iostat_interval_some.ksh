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

# `zpool iostat <pools> <N>` should keep running and only show the listed pools.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_iostat/zpool_iostat.kshlib

typeset vdev1=$(mktemp)
typeset vdev2=$(mktemp)

function cleanup {
	cleanup_iostat

	poolexists pool1 && destroy_pool pool1
	poolexists pool2 && destroy_pool pool2
	rm -f $vdev1 $vdev2
}

log_must mkfile $MINVDEVSIZE $vdev1 $vdev2

log_must zpool create pool1 $vdev1
delay_iostat

expect_iostat "HEADER"
expect_iostat "POOL1"
start_iostat pool1
delay_iostat

log_must zpool create pool2 $vdev2
delay_iostat

expect_iostat "NOPOOL"
log_must zpool export -a
delay_iostat

log_must zpool import -d $vdev2 pool2
delay_iostat

expect_iostat "HEADER"
expect_iostat "POOL1"
log_must zpool import -d $vdev1 pool1
delay_iostat

expect_iostat "NOPOOL"
log_must zpool destroy pool1
delay_iostat

log_must zpool destroy pool2
delay_iostat

stop_iostat

verify_iostat

log_pass "zpool iostat in interval mode with pools follows listed pool updates"
