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
# Verify that we use larger gang headers on ashift=12 pools
#
# Strategy:
# 1. Create a pool with dynamic gang headers.
# 2. Set metaslab_force_ganging to force ganging.
# 3. Verify that a large file has more than 3 gang headers.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "Verify that we don't use large gang headers on small-ashift pools".

log_onexit cleanup
preamble

for vdevtype in "" "mirror" "raidz" "raidz2" "draid"; do
	log_must zpool create -f -o ashift=12 $TESTPOOL $vdevtype $DISKS
	log_must zfs create -o recordsize=1M $TESTPOOL/$TESTFS
	mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
	set_tunable64 METASLAB_FORCE_GANGING 200000
	set_tunable32 METASLAB_FORCE_GANGING_PCT 100

	status=$(get_pool_prop feature@dynamic_gang_header $TESTPOOL)
	[[ "$status" == "enabled" ]] || \
		log_fail "Dynamic gang headers not enabled"
	path="${mountpoint}/file"
	log_must dd if=/dev/urandom of=$path bs=1M count=1
	log_must zpool sync $TESTPOOL
	first_block=$(get_first_block_dva $TESTPOOL/$TESTFS file)
	leaves=$(read_gang_header $TESTPOOL $first_block 1000 | \
		grep -v HOLE | grep -v "^Found")
	first_child=$(echo "$leaves" | head -n 1)
	check_gang_bp $first_child

	num_leaves=$(echo "$leaves" | wc -l)
	[[ "$num_leaves" -gt 3 ]] && \
		log_fail "used a larger gang header too soon: \"$leaves\""
	log_must verify_pool $TESTPOOL
	status=$(get_pool_prop feature@dynamic_gang_header $TESTPOOL)
	[[ "$status" == "active" ]] || log_fail "Dynamic gang headers not active"

	path="${mountpoint}/file2"
	log_must dd if=/dev/urandom of=$path bs=1M count=1
	log_must zpool sync $TESTPOOL
	first_block=$(get_first_block_dva $TESTPOOL/$TESTFS file2)
	leaves=$(read_gang_header $TESTPOOL $first_block 1000 | \
		grep -v HOLE | grep -v "^Found")
	first_child=$(echo "$leaves" | head -n 1)
	check_not_gang_bp $first_child

	num_leaves=$(echo "$leaves" | wc -l)
	[[ "$num_leaves" -gt 3 ]] || \
		log_fail "didn't use a larger gang header: \"$leaves\""


	log_must verify_pool $TESTPOOL
	status=$(get_pool_prop feature@dynamic_gang_header $TESTPOOL)
	[[ "$status" == "active" ]] || log_fail "Dynamic gang headers not active"
	log_must zpool destroy $TESTPOOL
done
log_pass "We don't use large gang headers on small-ashift pools".
