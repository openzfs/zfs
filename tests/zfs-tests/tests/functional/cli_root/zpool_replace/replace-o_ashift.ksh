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
# Copyright 2017, loli10K. All rights reserved.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	'zpool replace -o ashift=<n> ...' should work with different ashift
#	values.
#
# STRATEGY:
#	1. Create various pools with different ashift values.
#	2. Verify 'replace' works.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 VDEV_FILE_PHYSICAL_ASHIFT $orig_ashift
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $disk1 $disk2
}

log_assert "zpool replace -o ashift=<n>' works with different ashift values"
log_onexit cleanup

disk1=$TEST_BASE_DIR/disk1
disk2=$TEST_BASE_DIR/disk2
log_must truncate -s $SIZE $disk1
log_must truncate -s $SIZE $disk2

orig_ashift=$(get_tunable VDEV_FILE_PHYSICAL_ASHIFT)
#
# Set the file vdev's ashift to the max. Overriding
# the ashift using the -o ashift property should still
# be honored.
#
log_must set_tunable32 VDEV_FILE_PHYSICAL_ASHIFT 16

typeset ashifts=("9" "10" "11" "12" "13" "14" "15" "16")
for ashift in ${ashifts[@]}
do
	log_must zpool create -o ashift=$ashift $TESTPOOL1 $disk1
	log_must verify_ashift $disk1 $ashift
	# ashift_of(replacing_disk) <= ashift_of(existing_vdev)
	log_must zpool replace $TESTPOOL1 $disk1 $disk2
	log_must verify_ashift $disk2 $ashift
	wait_replacing $TESTPOOL1
	# clean things for the next run
	log_must zpool destroy $TESTPOOL1
	log_must zpool labelclear $disk1
	log_must zpool labelclear $disk2
done

typeset badvals=("off" "on" "1" "8" "17" "1b" "ff" "-")
for badval in ${badvals[@]}
do
	log_must zpool create $TESTPOOL1 $disk1
	log_mustnot zpool replace -o ashift=$badval $TESTPOOL1 $disk1 $disk2
	log_must zpool destroy $TESTPOOL1
	log_must zpool labelclear $disk1
	log_mustnot zpool labelclear $disk2
done

log_pass "zpool replace -o ashift=<n>' works with different ashift values"
