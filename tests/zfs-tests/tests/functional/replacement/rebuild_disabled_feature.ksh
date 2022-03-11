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
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# Description:
# Verify device_rebuild feature flags.
#
# Strategy:
# 1. Create a pool with all features disabled.
# 2. Verify 'zpool replace -s' fails and the feature is disabled.
# 3. Enable the device_rebuild feature.
# 4. Verify 'zpool replace -s' works and the feature is active.
# 5. Wait for the feature to return to enabled.
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE
}

function check_feature_flag
{
	feature=$1
	pool=$2
	expected_value=$3

	value="$(zpool get -H -o property,value all $pool | awk -v f="$feature" '$0 ~ f {print $2}')"
	if [ "$value" = "$expected_value" ]; then
		log_note "$feature verified to be $value"
	else
		log_fail "$feature should be $expected_value but is $value"
	fi
}

log_assert "Verify device_rebuild feature flags."

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)

log_onexit cleanup

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[@]} $SPARE_VDEV_FILE
log_must zpool create -d $TESTPOOL1 ${VDEV_FILES[@]}

log_mustnot zpool replace -s $TESTPOOL1 ${VDEV_FILES[1]} $SPARE_VDEV_FILE
check_feature_flag "feature@device_rebuild" "$TESTPOOL1" "disabled"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool set feature@device_rebuild=enabled $TESTPOOL1
log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[1]} $SPARE_VDEV_FILE
check_feature_flag "feature@device_rebuild" "$TESTPOOL1" "active"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
log_must zpool wait -t resilver $TESTPOOL1
check_feature_flag "feature@device_rebuild" "$TESTPOOL1" "enabled"

log_pass "Verify device_rebuild feature flags."
