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
# Copyright (c) 2023 by iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zpool attach poolname raidz ...' should fail if raidz_expansion
#	feature is not enabled, also when several devices are given and
#	raidz_expansion_multi is enabled.
#
# STRATEGY:
#	1. Create raidz pool with raidz_expansion feature disabled
#	2. Attempt to attach a device to the raidz vdev
#	3. Verify that device attached failed
#	4. Attempt to attach two devices to the raidz vdev
#	5. Verify that it failed, naming the raidz_expansion feature, and
#	   that the raidz vdev did not change
#	6. Destroy the raidz pool

typeset -r devs=4
typeset -r dev_size_mb=128
typeset -a disks

function cleanup
{
	log_pos zpool status "$TESTPOOL"

	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$((devs + 1))}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

log_onexit cleanup

for i in {0..$((devs + 1))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M "$device"
	if [[ $i -lt $devs ]]; then
		disks[${#disks[*]}+1]=$device
	fi
done

# create a pool with raidz_expansion feature disabled
log_must zpool create -f -o cachefile=none -o feature@raidz_expansion=disabled \
	"$TESTPOOL" raidz1 "${disks[@]}"
status=$(zpool list -H -o feature@raidz_expansion "$TESTPOOL")
if [[ "$status" != "disabled" ]]; then
	log_fail "raidz_expansion feature was not disabled"
fi

# expecting attach to fail
log_mustnot_expect "raidz_expansion feature must be enabled" zpool attach -f \
	"$TESTPOOL" raidz1-0 "$TEST_BASE_DIR/dev-$devs"

# several devices at once: raidz_expansion is checked first, so this fails
# even though raidz_expansion_multi is enabled
status=$(zpool list -H -o feature@raidz_expansion_multi "$TESTPOOL")
if [[ "$status" != "enabled" ]]; then
	log_fail "raidz_expansion_multi feature was not enabled"
fi
log_mustnot_expect "raidz_expansion and raidz_expansion_multi" \
	zpool attach -f "$TESTPOOL" raidz1-0 "$TEST_BASE_DIR/dev-$devs" \
	"$TEST_BASE_DIR/dev-$((devs + 1))"
width=$(zpool status "$TESTPOOL" | grep -c "$TEST_BASE_DIR/dev-")
if [[ $width -ne $devs ]]; then
	log_fail "expected $devs disks after the refused attach, found $width"
fi
log_must zpool destroy "$TESTPOOL"

log_pass "raidz attach failed with feature disabled as expected"
