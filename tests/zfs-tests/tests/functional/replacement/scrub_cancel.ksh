#!/bin/ksh -p

#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# Verify scrub behaves as intended when contending with a healing or
# sequential resilver.
#
# STRATEGY:
# 1. Create a pool
# 2. Add a modest amount of data to the pool.
# 3. For healing and sequential resilver:
#    a. Start scrubbing.
#    b. Verify a resilver can be started and it cancels the scrub.
#    c. Verify a scrub cannot be started when resilvering
#

function cleanup
{
	log_must set_tunable32 RESILVER_MIN_TIME_MS $ORIG_RESILVER_MIN_TIME
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE
}

log_assert "Scrub was cancelled by resilver"

ORIG_RESILVER_MIN_TIME=$(get_tunable RESILVER_MIN_TIME_MS)
ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)

log_onexit cleanup

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[@]} $SPARE_VDEV_FILE

log_must zpool create -f $TESTPOOL1 ${VDEV_FILES[@]}
log_must zfs create $TESTPOOL1/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=64
sync_pool $TESTPOOL1

# Request a healing or sequential resilver
for replace_mode in "healing" "sequential"; do

	#
	# Healing resilvers abort the dsl_scan and reconfigure it for
	# resilvering.  Sequential resilvers cancel the dsl_scan and start
	# the vdev_rebuild thread.
	#
	if [[ "$replace_mode" = "healing" ]]; then
		history_msg="scan aborted, restarting"
		flags=""
	else
		history_msg="scan cancelled"
		flags="-s"
	fi

	# Limit scanning time and suspend the scan as soon as possible.
	log_must set_tunable32 RESILVER_MIN_TIME_MS 50
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

	# Initiate a scrub.
	log_must zpool scrub $TESTPOOL1

	# Initiate a resilver to cancel the scrub.
	log_must zpool replace $flags $TESTPOOL1 ${VDEV_FILES[1]} \
	    $SPARE_VDEV_FILE

	# Verify the scrub was canceled, it may take a few seconds to exit.
	while is_pool_scrubbing $TESTPOOL1; do
		sleep 1
	done
	log_mustnot is_pool_scrubbing $TESTPOOL1

	# Verify a scrub cannot be started while resilvering.
	log_must is_pool_resilvering $TESTPOOL1
	log_mustnot zpool scrub $TESTPOOL1

	# Unsuspend resilver.
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must set_tunable32 RESILVER_MIN_TIME_MS 3000

	# Wait for resilver to finish then put the original back.
	log_must zpool wait $TESTPOOL1
	log_must zpool replace $flags -w $TESTPOOL1 $SPARE_VDEV_FILE \
	    ${VDEV_FILES[1]}
done
log_pass "Scrub was cancelled by resilver"

