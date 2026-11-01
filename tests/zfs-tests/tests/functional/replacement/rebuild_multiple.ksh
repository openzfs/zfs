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
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# Sequential reconstruction (unlike healing reconstruction) operate on the
# top-level vdev.  This means that a sequential resilver operation can be
# started/stopped on a different top-level vdev without impacting other
# sequential resilvers.
#
# STRATEGY:
# 1. Create a mirrored pool.
#

function cleanup
{
	[[ -n "$fault" ]] && log_must zinject -c "$fault"
	[[ -n "$fault2" ]] && log_must zinject -c "$fault2"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	log_must set_tunable64 REBUILD_VDEV_LIMIT "$rebuild_limit"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE $SPARE_VDEV_FILE2
}

function check_history
{
	pool=$1
	msg=$2
	exp=$3

	count=$(zpool history -i $pool | grep "rebuild" | grep -c "$msg")
	if [[ "$count" -ne "$exp" ]]; then
		log_fail "Expected $exp rebuild '$msg' messages, found $count"
	else
		log_note "Found $count/$exp rebuild '$msg' messages"
	fi
}

log_assert "Rebuilds operate on the top-level vdevs"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
rebuild_limit=$(get_tunable REBUILD_VDEV_LIMIT)
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED)

log_onexit cleanup
log_must set_tunable32 REBUILD_SCRUB_ENABLED 1

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[@]} \
    $SPARE_VDEV_FILE $SPARE_VDEV_FILE2

# Verify two sequential resilvers can run concurrently.
log_must zpool create -f $TESTPOOL1 \
    mirror ${VDEV_FILES[0]} ${VDEV_FILES[1]} \
    mirror ${VDEV_FILES[2]} ${VDEV_FILES[3]}

mntpnt=$(get_prop mountpoint "$TESTPOOL1")
log_must file_write -o create -f "$mntpnt/file" -b 1048576 -c 32 -d 66
sync_pool $TESTPOOL1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[1]} $SPARE_VDEV_FILE
log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[3]} $SPARE_VDEV_FILE2

check_history $TESTPOOL1 "started" 2
check_history $TESTPOOL1 "reset" 0
check_history $TESTPOOL1 "complete" 0
check_history $TESTPOOL1 "canceled" 0

log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
log_must zpool wait -t resilver $TESTPOOL1

check_history $TESTPOOL1 "complete" 2
destroy_pool $TESTPOOL1

# Verify canceling one resilver (zpool detach) does not impact others.
log_must zpool create -f $TESTPOOL1 \
    mirror ${VDEV_FILES[0]} ${VDEV_FILES[1]} \
    mirror ${VDEV_FILES[2]} ${VDEV_FILES[3]}

mntpnt=$(get_prop mountpoint "$TESTPOOL1")
log_must file_write -o create -f "$mntpnt/file" -b 1048576 -c 32 -d 66
sync_pool $TESTPOOL1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[1]} $SPARE_VDEV_FILE
log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[3]} $SPARE_VDEV_FILE2

check_history $TESTPOOL1 "started" 2
check_history $TESTPOOL1 "reset" 0
check_history $TESTPOOL1 "complete" 0
check_history $TESTPOOL1 "canceled" 0

log_must zpool detach $TESTPOOL1 $SPARE_VDEV_FILE2

check_history $TESTPOOL1 "complete" 0
check_history $TESTPOOL1 "canceled" 1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
log_must zpool wait -t resilver $TESTPOOL1

check_history $TESTPOOL1 "complete" 1
check_history $TESTPOOL1 "canceled" 1
destroy_pool $TESTPOOL1

log_note "Pending verification survives import and the last rebuild's cancellation"
log_must zpool create -f "$TESTPOOL1" \
    mirror "${VDEV_FILES[0]}" "${VDEV_FILES[1]}" \
    mirror "${VDEV_FILES[2]}" "${VDEV_FILES[3]}"
mntpnt=$(get_prop mountpoint "$TESTPOOL1")
log_must file_write -o create -f "$mntpnt/file" -b 1048576 -c 32 -d 66
sync_pool "$TESTPOOL1"
log_must set_tunable64 REBUILD_VDEV_LIMIT $((1024 * 1024))
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s "$TESTPOOL1" "${VDEV_FILES[1]}" "$SPARE_VDEV_FILE"
log_must zpool replace -s "$TESTPOOL1" "${VDEV_FILES[3]}" "$SPARE_VDEV_FILE2"
fault=$(zinject -q -d "${VDEV_FILES[2]}" -D 500:1 -T read "$TESTPOOL1") ||
    log_fail "cannot delay the second rebuild"
fault2=$(zinject -q -d "${VDEV_FILES[3]}" -D 500:1 -T read "$TESTPOOL1") ||
    log_fail "cannot delay the second rebuild's other source"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
for ((i = 0; i < 30; i++)); do
	zpool history -i "$TESTPOOL1" |
	    grep -q "rebuild.*vdev_id=0 .*complete" && break
	sleep 1
done
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
check_history "$TESTPOOL1" "complete" 1
check_history "$TESTPOOL1" "canceled" 0
scans=$(zpool history -i "$TESTPOOL1" | grep -c "scan setup func=1")
log_must test "$scans" -eq 0
log_must zinject -c "$fault"
fault=
log_must zinject -c "$fault2"
fault2=
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
log_must is_pool_resilvering "$TESTPOOL1"
log_must zpool detach "$TESTPOOL1" "$SPARE_VDEV_FILE2"
check_history "$TESTPOOL1" "complete" 1
check_history "$TESTPOOL1" "canceled" 1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t scrub "$TESTPOOL1"
verify_pool_scrubbed "$TESTPOOL1"
destroy_pool "$TESTPOOL1"

log_note "Canceling a rebuild without a completion does not request a scrub"
log_must zpool create -f "$TESTPOOL1" \
    mirror "${VDEV_FILES[0]}" "${VDEV_FILES[1]}"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s "$TESTPOOL1" "${VDEV_FILES[1]}" "$SPARE_VDEV_FILE"
log_must zpool detach "$TESTPOOL1" "$SPARE_VDEV_FILE"
log_must zpool sync "$TESTPOOL1"
check_history "$TESTPOOL1" "complete" 0
scans=$(zpool history -i "$TESTPOOL1" | grep -c "scan setup func=1")
log_must test "$scans" -eq 0
destroy_pool "$TESTPOOL1"

log_pass "Rebuilds operate on the top-level vdevs"
