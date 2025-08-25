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
# Verify that the redundant_metadata setting is respected by gang headers
#
# Strategy:
# 1. Create a filesystem with redundant_metadata={all,most,some,none}
# 2. Verify that gang blocks at different levels have the right amount of redundancy
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "Verify that gang blocks at different levels have the right amount of redundancy."

function cleanup2
{
	for red in all most some none; do zfs destroy $TESTPOOL/$TESTFS-$red; done
	cleanup
}

preamble
log_onexit cleanup2

log_must zpool create -f -o ashift=9 $TESTPOOL $DISKS
set_tunable64 METASLAB_FORCE_GANGING 1500
set_tunable32 METASLAB_FORCE_GANGING_PCT 100
for red in all most some none; do
	log_must zfs create -o redundant_metadata=$red -o recordsize=512 \
		 $TESTPOOL/$TESTFS-$red
	if [[ "$red" == "all" ]]; then
		log_must zfs set recordsize=8k $TESTPOOL/$TESTFS-$red
	fi
	mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS-$red)

	path="${mountpoint}/file"
	log_must dd if=/dev/urandom of=$path bs=1M count=1
	log_must zpool sync $TESTPOOL
	num_l0_dvas=$(get_first_block $TESTPOOL/$TESTFS-$red file | get_num_dvas)
	if [[ "$red" == "all" ]]; then
		[[ "$num_l0_dvas" -eq 2 ]] || \
			log_fail "wrong number of DVAs for L0 in $red: $num_l0_dvas"
	else
		[[ "$num_l0_dvas" -eq 1 ]] || \
			log_fail "wrong number of DVAs for L0 in $red: $num_l0_dvas"
	fi

	num_l1_dvas=$(get_blocks_filter $TESTPOOL/$TESTFS-$red file L1 | head -n 1 | get_num_dvas)
	if [[ "$red" == "all" || "$red" == "most" ]]; then
		[[ "$num_l1_dvas" -eq 2 ]] || \
			log_fail "wrong number of DVAs for L1 in $red: $num_l1_dvas"
	else
		[[ "$num_l1_dvas" -eq 1 ]] || \
			log_fail "wrong number of DVAs for L1 in $red: $num_l1_dvas"
	fi

	for i in `seq 1 80`; do
		dd if=/dev/urandom of=/$mountpoint/f$i bs=512 count=1 2>/dev/null || log_fail "dd failed"
	done
	log_must zpool sync $TESTPOOL
	obj_0_gangs=$(get_object_info $TESTPOOL/$TESTFS-$red 0 L0 | grep G)
	num_obj_0_dvas=$(echo "$obj_0_gangs" | head -n 1 | get_num_dvas)
	if [[ "$red" != "none" ]]; then
		[[ "$num_obj_0_dvas" -eq 2 ]] || \
			log_fail "wrong number of DVAs for obj 0 in $red: $num_obj_0_dvas"
	else
		[[ "$num_obj_0_dvas" -eq 1 ]] || \
			log_fail "wrong number of DVAs for obj 0 in $red: $num_obj_0_dvas"
	fi
	log_note "Level $red passed"
done

log_pass "Gang blocks at different levels have the right amount of redundancy."
