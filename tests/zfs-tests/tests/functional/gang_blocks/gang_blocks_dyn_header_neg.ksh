#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2025 by Klara Inc.
#

#
# Description:
# Verify that we don't use larger gang headers on ashift=9 pools
#
# Strategy:
# 1. Create a pool with dynamic gang headers.
# 2. Set metaslab_force_ganging to force multi-level ganging.
# 3. Verify that a large file has multi-level ganging
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "Verify that we don't use large gang headers on small-ashift pools".

log_onexit cleanup
preamble

log_must zpool create -f -o ashift=9 -o feature@dynamic_gang_header=enabled $TESTPOOL $DISKS
log_must zfs create -o recordsize=1M $TESTPOOL/$TESTFS
mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
set_tunable64 METASLAB_FORCE_GANGING 200000
set_tunable32 METASLAB_FORCE_GANGING_PCT 100

path="${mountpoint}/file"
log_must dd if=/dev/urandom of=$path bs=1M count=1
log_must zpool sync $TESTPOOL
first_block=$(get_first_block_dva $TESTPOOL/$TESTFS file)
leaves=$(read_gang_header $TESTPOOL $first_block 200)
gangs=$(echo "$leaves" | grep -c gang)
[[ "$gangs" -gt 0 ]] || log_fail "We didn't use a deep gang tree when needed"

log_must verify_pool $TESTPOOL
status=$(get_pool_prop feature@dynamic_gang_header $TESTPOOL)
[[ "$status" == "enabled" ]] || log_fail "Dynamic gang headers active on an ashift-9 pool"
log_pass "We don't use large gang headers on small-ashift pools".
