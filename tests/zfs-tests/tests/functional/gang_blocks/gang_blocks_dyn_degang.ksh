#!/bin/ksh
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
# Verify that deganging works correctly
#
# Strategy:
# 1. Create a pool with dynamic gang headers and ashift=12.
# 2. Set metaslab_force_ganging to force ganging.
# 3. Set metaslab_allow_deganging_pct to allow it to degang.
# 3. Verify that the file degangs successfully.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "Verify that deganging works correctly"

log_onexit cleanup

log_must zpool create -f -o ashift=12 -o feature@dynamic_gang_header=enabled $TESTPOOL $DISKS
log_must zfs create -o recordsize=1M $TESTPOOL/$TESTFS
mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
set_tunable64 METASLAB_FORCE_GANGING 50000
set_tunable32 METASLAB_FORCE_GANGING_PCT 100
set_tunable32 METASLAB_ALLOW_DEGANGING_PCT 100

path="${mountpoint}/file"
log_must dd if=/dev/urandom of=$path bs=1M count=1
log_must zpool sync $TESTPOOL
first_block=$(get_first_block $TESTPOOL/$TESTFS file)
check_not_gang_dva $first_block

log_must verify_pool $TESTPOOL
status=$(get_pool_prop feature@dynamic_gang_header $TESTPOOL)
[[ "$status" == "enabled" ]] || log_fail "Dynamic gang headers should not be active."

log_pass "Deganging works correctly."
