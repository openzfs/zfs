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
# Copyright (c) 2026 by Skountz. All rights reserved.
#


. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that the expansion dspace correction is not applied to a
#	non-allocating RAIDZ vdev.
#
#	A non-allocating vdev's space is excluded from the pool's
#	available space.  If the free space of an expanded RAIDZ vdev
#	were still credited at the new ratio, the pool would report more
#	available space than the allocating vdevs can provide, and
#	filling it would fail in syncing context.
#
# STRATEGY:
#	1. Create a pool with a RAIDZ1 vdev of 3 disks and a plain disk
#	2. Write data and expand the RAIDZ1 vdev to 4 disks
#	3. Set allocating=off on the RAIDZ1 vdev
#	4. Verify the pool's available space does not exceed the free
#	   space of the plain disk
#	5. Set allocating=on and verify available space grows back
#

typeset -r dev_size_mb=1024

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..4}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

log_onexit cleanup

for i in {0..4}; do
	log_must truncate -s ${dev_size_mb}M "$TEST_BASE_DIR/dev-$i"
done

log_must zpool create -f -o cachefile=none "$TESTPOOL" \
    raidz1 "$TEST_BASE_DIR/dev-0" "$TEST_BASE_DIR/dev-1" \
    "$TEST_BASE_DIR/dev-2"
log_must zpool add -f "$TESTPOOL" "$TEST_BASE_DIR/dev-4"
log_must zfs create -o compression=off -o recordsize=128k "$TESTPOOL/fs"

log_must dd if=/dev/urandom of="/$TESTPOOL/fs/file" bs=1M count=200
log_must zpool sync "$TESTPOOL"

log_must zpool attach -w "$TESTPOOL" raidz1-0 "$TEST_BASE_DIR/dev-3"
log_must zpool sync "$TESTPOOL"

avail_on=$(zfs get -Hp -o value available "$TESTPOOL")

log_must zpool set allocating=off "$TESTPOOL" raidz1-0
log_must zpool sync "$TESTPOOL"

avail_off=$(zfs get -Hp -o value available "$TESTPOOL")
plain_free=$(zpool list -Hpv -o name,free "$TESTPOOL" | \
    awk -v d="$TEST_BASE_DIR/dev-4" '$1 == d {print $2}')

log_note "available=$avail_off plain disk free=$plain_free"
if [[ $avail_off -gt $plain_free ]]; then
	log_fail "available ($avail_off) exceeds the free space of the" \
	    "only allocating vdev ($plain_free)"
fi

log_must zpool set allocating=on "$TESTPOOL" raidz1-0
log_must zpool sync "$TESTPOOL"

avail_back=$(zfs get -Hp -o value available "$TESTPOOL")
log_note "available before=$avail_on after=$avail_back"
if [[ $avail_back -le $avail_off ]]; then
	log_fail "available ($avail_back) did not grow after re-enabling" \
	    "allocation ($avail_off)"
fi

log_pass "Expansion dspace correction skips non-allocating vdevs."
