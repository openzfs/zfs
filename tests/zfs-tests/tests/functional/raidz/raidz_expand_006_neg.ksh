#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
