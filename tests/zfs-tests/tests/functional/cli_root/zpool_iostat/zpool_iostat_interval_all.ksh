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

# `zpool iostat <N>` should keep running and update the pools it displays as
# pools are created/destroyed/imported/export.

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

expect_iostat "NOPOOL"

start_iostat

delay_iostat

expect_iostat "HEADER"
expect_iostat "POOL1"
log_must zpool create pool1 $vdev1
delay_iostat

expect_iostat "HEADER"
expect_iostat "POOLBOTH"
log_must zpool create pool2 $vdev2
delay_iostat

# Export the pools one at a time rather than with a single "zpool export -a".
# iostat samples every 0.1s and "export -a" tears the pools down
# sequentially, so it would sometimes catch the intermediate one-pool state
# and emit an extra chunk that is not in the expected output. Exporting each
# pool explicitly makes that transition deterministic, mirroring the
# one-pool-at-a-time imports below.
expect_iostat "HEADER"
expect_iostat "POOL2"
log_must zpool export pool1
delay_iostat

expect_iostat "NOPOOL"
log_must zpool export pool2
delay_iostat

expect_iostat "HEADER"
expect_iostat "POOL2"
log_must zpool import -d $vdev2 pool2
delay_iostat

expect_iostat "HEADER"
expect_iostat "POOLBOTH"
log_must zpool import -d $vdev1 pool1
delay_iostat

expect_iostat "HEADER"
expect_iostat "POOL2"
log_must zpool destroy pool1
delay_iostat

expect_iostat "NOPOOL"
log_must zpool destroy pool2
delay_iostat

stop_iostat

verify_iostat

log_pass "zpool iostat in interval mode follows pool updates"
