#!/bin/ksh -p
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
# 	Negative for FreeBSD Only
#
#	Attempting to expand a RAIDZ should fail if the scratch area on the
#	existing disks contains BTX Server binary (used to boot FreeBSD when
#	using MBR partitions with ZFS).
#
# STRATEGY:
#	1. Create raidz pool
#	2. Add a BTX header to the reserved boot area
#	3. Attempt to attach a device to the raidz vdev
#	4. Verify that device attached failed
#	5. Destroy the raidz pool

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
	# simulate active BTX Server data by inserting a BTX header
	printf "\xeb\x0e%s\x01\x02\x80" "BTX" | dd of="$device" \
		bs=512 seek=1024 status=none
	log_must truncate -s ${dev_size_mb}M "$device"
	if [[ $i -ne $devs ]]; then
		disks[${#disks[*]}+1]=$device
	fi
done

log_must zpool create -f -o cachefile=none "$TESTPOOL" raidz1 "${disks[@]}"

if is_freebsd; then
	# expecting attach to fail
	log_mustnot_expect "the reserved boot area" zpool attach -f \
		"$TESTPOOL" raidz1-0 "$TEST_BASE_DIR/dev-$devs"
	log_must zpool destroy "$TESTPOOL"
	log_pass "raidz attach failed with in-use reserved boot area"
else
	# expecting attach to pass everywhere else
	log_must zpool attach -f "$TESTPOOL" raidz1-0 "$TEST_BASE_DIR/dev-$devs"
	log_must zpool destroy "$TESTPOOL"
	log_pass "raidz attach passed with in-use reserved boot area"
fi

