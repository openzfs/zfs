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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# A sequential resilver is not canceled by the "zpool scrub" commands that
# are refused while it runs.  Each of them reopens every vdev before it is
# refused, and a leaf being reopened still needs the resilver.  A canceled
# resilver leaves the replacing vdev behind, and "zpool wait" never returns.
#
# STRATEGY:
# 1. Create a pool of four file vdevs with many metaslabs each, since the
#    resilver checks once per metaslab whether it is still needed, and
#    add some data.
# 2. Replace a device with "zpool replace -s", the resilver suspended, and
#    verify that a scrub is refused.
# 3. Request scrubs in background loops and let the resilver run.
# 4. Verify, with bounded waits, that the resilver completes rather than
#    being canceled, and that the replacing vdev goes away.
# 5. Replace the device back the same way, three times over each way,
#    then verify that the pool is healthy.
#

verify_runnable "global"

STOP_FILE=$TEST_BASE_DIR/rebuild_reopen_stop

function cleanup
{
	touch $STOP_FILE
	wait
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	log_must set_tunable64 VDEV_MIN_MS_COUNT $ORIG_VDEV_MIN_MS_COUNT
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE $STOP_FILE
}

function replace_while_scrubbing # <old device> <new device>
{
	typeset -i i

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace -s $TESTPOOL1 $1 $2
	log_must is_pool_resilvering $TESTPOOL1
	log_mustnot zpool scrub $TESTPOOL1

	# Once the resilver has completed, a scrub may be accepted.
	log_must rm -f $STOP_FILE
	for i in {1..4}; do
		while [[ ! -e $STOP_FILE ]]; do
			zpool scrub $TESTPOOL1 >/dev/null 2>&1
		done &
	done

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	for (( i = 0; i < 120; i++ )); do
		is_pool_resilvering $TESTPOOL1 || break
		sleep 1
	done
	log_must touch $STOP_FILE
	wait

	log_mustnot is_pool_resilvering $TESTPOOL1
	if zpool history -i $TESTPOOL1 | grep -q "rebuild.*canceled"; then
		log_fail "The sequential resilver was canceled"
	fi
	wait_replacing $TESTPOOL1 60
	log_mustnot is_pool_replacing $TESTPOOL1
}

log_assert "Refused scrubs do not cancel a sequential resilver"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_VDEV_MIN_MS_COUNT=$(get_tunable VDEV_MIN_MS_COUNT)

log_onexit cleanup

log_must set_tunable64 VDEV_MIN_MS_COUNT 64
log_must truncate -s $((VDEV_FILE_SIZE * 8)) ${VDEV_FILES[@]} \
    $SPARE_VDEV_FILE

log_must zpool create -f $TESTPOOL1 ${VDEV_FILES[@]}
log_must zfs create $TESTPOOL1/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=64
sync_pool $TESTPOOL1

for i in {1..3}; do
	replace_while_scrubbing ${VDEV_FILES[1]} $SPARE_VDEV_FILE
	replace_while_scrubbing $SPARE_VDEV_FILE ${VDEV_FILES[1]}
done

log_must check_state $TESTPOOL1 "" "online"
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "Refused scrubs do not cancel a sequential resilver"
