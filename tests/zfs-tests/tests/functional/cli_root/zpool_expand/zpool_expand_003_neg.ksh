#! /bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#


. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# Description:
# Once set zpool autoexpand=off, zpool can *NOT* autoexpand by
# Dynamic VDEV Expansion
#
# STRATEGY:
# 1) Create three vdevs (loopback, scsi_debug, and file)
# 2) Create pool by using the different devices and set autoexpand=off
# 3) Expand each device as appropriate
# 4) Check that the pool size is not expanded
#
# NOTE: Three different device types are used in this test to verify
# expansion of non-partitioned block devices (loopback), partitioned
# block devices (scsi_debug), and non-disk file vdevs.  ZFS volumes
# are not used in order to avoid a possible lock inversion when
# layering pools on zvols.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	if losetup -a | grep -q $DEV1; then
		losetup -d $DEV1
	fi

	rm -f $FILE_LO $FILE_RAW

	block_device_wait
	unload_scsi_debug
}

log_onexit cleanup

log_assert "zpool can not expand if set autoexpand=off after vdev expansion"

for type in "" mirror raidz draid; do
	log_note "Setting up loopback, scsi_debug, and file vdevs"
	log_must truncate -s $org_size $FILE_LO
	DEV1=$(losetup -f)
	log_must losetup $DEV1 $FILE_LO

	load_scsi_debug $org_size_mb 1 1 1 '512b'
	block_device_wait
	DEV2=$(get_debug_device)

	log_must truncate -s $org_size $FILE_RAW
	DEV3=$FILE_RAW

	# The -f is required since we're mixing disk and file vdevs.
	log_must zpool create -f $TESTPOOL1 $type $DEV1 $DEV2 $DEV3

	log_must [ "$(get_pool_prop autoexpand $TESTPOOL1)" = "off" ]

	typeset prev_size=$(get_pool_prop size $TESTPOOL1)


	# Expand each device as appropriate being careful to add an artificial
	# delay to ensure we get a single history entry for each.  This makes
	# is easier to verify each expansion for the striped pool case, since
	# they will not be merged in to a single larger expansion.
	log_note "Expanding loopback, scsi_debug, and file vdevs"
	log_must truncate -s $exp_size $FILE_LO
	log_must losetup -c $DEV1
	sleep 3

	log_must eval "echo 2 > /sys/bus/pseudo/drivers/scsi_debug/virtual_gb"
	log_must eval "echo 1 > /sys/class/block/$DEV2/device/rescan"
	block_device_wait
	sleep 3

	log_must truncate -s $exp_size $FILE_RAW

	# This is far longer than we should need to wait, but let's be sure.
	sleep 5

	# check for zpool history for the pool size expansion
	zpool history -il $TESTPOOL1 | grep "pool '$TESTPOOL1' size:" |
	    grep "vdev online" &&
	    log_fail "pool $TESTPOOL1 is not autoexpand after vdev expansion"

	log_must [ "$(get_pool_prop size $TESTPOOL1)" = "$prev_size" ]

	cleanup
done

log_pass "zpool can not autoexpand if autoexpand=off after vdev expansion"
