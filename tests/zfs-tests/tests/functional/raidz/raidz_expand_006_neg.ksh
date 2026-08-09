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
#	feature is not enabled.
#
# STRATEGY:
#	1. Create raidz pool with raidz_expansion feature disabled
#	2. Attempt to attach a device to the raidz vdev
#	3. Verify that device attached failed
#	4. Destroy the raidz pool

typeset -r devs=4
typeset -r dev_size_mb=128
typeset -a disks

function cleanup
{
	log_pos zpool status "$TESTPOOL"

	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

log_onexit cleanup

for i in {0..$devs}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M "$device"
	if [[ $i -ne $devs ]]; then
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
log_must zpool destroy "$TESTPOOL"

log_pass "raidz attach failed with feature disabled as expected"
