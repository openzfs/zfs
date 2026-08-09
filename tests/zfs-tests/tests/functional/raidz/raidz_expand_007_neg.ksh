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

