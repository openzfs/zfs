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
# http://www.illumos.org/license/CDDL.
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	Import with missing log device should not remove spare/cache.
#
# STRATEGY:
#	1. Create a pool.
#	2. Add spare, cache and log devices to the pool.
#	3. Export the pool.
#	4. Remove the log device.
#	5. Import the pool with -m flag.
#	6. Verify that spare and cache are still present in the pool.
#

verify_runnable "global"

log_onexit cleanup

function test_missing_log
{
	typeset poolcreate="$1"
	typeset cachevdev="$2"
	typeset sparevdev="$3"
	typeset logvdev="$4"
	typeset missingvdev="$4"

	log_note "$0: pool '$poolcreate', adding $cachevdev, $sparevdev," \
		"$logvdev then moving away $missingvdev."

	log_must zpool create $TESTPOOL1 $poolcreate

	log_must zpool add $TESTPOOL1 cache $cachevdev spare $sparevdev \
		log $logvdev

	log_must_busy zpool export $TESTPOOL1

	log_must mv $missingvdev $BACKUP_DEVICE_DIR

	log_must zpool import -m -d $DEVICE_DIR $TESTPOOL1

	CACHE_PRESENT=$(zpool status -v $TESTPOOL1 | grep $cachevdev)

	SPARE_PRESENT=$(zpool status -v $TESTPOOL1 | grep $sparevdev)

	if [ -z "$CACHE_PRESENT"] || [ -z "SPARE_PRESENT"]
	then
		log_fail "cache/spare vdev missing after importing with missing" \
			"log device"
	fi

	# Cleanup
	log_must zpool destroy $TESTPOOL1

	log_note ""
}

log_must mkdir -p $BACKUP_DEVICE_DIR

test_missing_log "$VDEV0" "$VDEV1" "$VDEV2" "$VDEV3"

log_pass "zpool import succeeded with missing log device"
