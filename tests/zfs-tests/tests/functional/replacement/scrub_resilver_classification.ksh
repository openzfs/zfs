#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

# shellcheck disable=SC1091

. "$STF_SUITE"/include/libtest.shlib
. "$STF_SUITE"/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
#	Verify a DTL-limited scrub preserves scrub controls while using healing
#	resilver I/O.
#
# STRATEGY:
#	1. Start a replacement with scan progress paused.
#	2. Complete its initial resilver under a checkpoint, preserving the
#	   replacement DTL, then discard the checkpoint.
#	3. Verify the DTL-limited scrub can be paused, resumed, and canceled.
#	4. Run it again with read errors injected on the DTL-missing child.
#	5. Verify scrub status, resilver I/O, repair, detach, and data integrity.
#

verify_runnable "global"

function inject_count
{
	zinject | awk '
		/^ *[0-9]/ {
			count++
			inject = $NF
		}
		END {
			if (count != 1)
				exit 1
			print inject
		}'
}

function cleanup
{
	zinject -c all >/dev/null 2>&1
	set_tunable32 SCAN_SUSPEND_PROGRESS \
	    "$ORIG_SCAN_SUSPEND_PROGRESS" >/dev/null 2>&1
	zpool checkpoint -d -w "$TESTPOOL1" >/dev/null 2>&1
	destroy_pool "$TESTPOOL1"
	rm -f "${VDEV_FILES[@]}" "$SPARE_VDEV_FILE"
}

log_assert "DTL-limited scrubs retain scrub controls and use resilver I/O"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)

log_onexit cleanup

log_must zinject -c all
log_must truncate -s "$VDEV_FILE_SIZE" \
    "${VDEV_FILES[0]}" "$SPARE_VDEV_FILE"
# Keep the replacement DTL available for the scrub instead of restarting a
# deferred resilver when the checkpointed initial scan completes.
log_must zpool create -f -o feature@resilver_defer=disabled \
    "$TESTPOOL1" "${VDEV_FILES[0]}"
log_must zfs create "$TESTPOOL1/$TESTFS"

mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
log_must dd if=/dev/urandom of="$mntpnt/file" bs=1M count=1
sync_pool "$TESTPOOL1"

# A checkpoint prevents the initial resilver from excising the target DTL.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace "$TESTPOOL1" "${VDEV_FILES[0]}" \
    "$SPARE_VDEV_FILE"
log_must is_pool_resilvering "$TESTPOOL1"
log_must zpool checkpoint "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must is_pool_resilvered "$TESTPOOL1"
log_must zpool checkpoint -d -w "$TESTPOOL1"

pool_status=$(zpool status -P "$TESTPOOL1") ||
    log_fail "unable to read pool status after checkpoint discard"
[[ "$pool_status" == *replacing-* &&
    "$pool_status" == *"${VDEV_FILES[0]}"* &&
    "$pool_status" == *"$SPARE_VDEV_FILE"* ]] ||
    log_fail "replacement topology was not preserved: $pool_status"

# The effective resilver must retain the controls of the requested scrub.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub -t "$TESTPOOL1"
log_must is_pool_scrubbing "$TESTPOOL1"
log_must zpool scrub -p "$TESTPOOL1"
log_must is_pool_scrub_paused "$TESTPOOL1"
log_must zpool scrub "$TESTPOOL1"
log_must is_pool_scrubbing "$TESTPOOL1"
log_must zpool scrub -s "$TESTPOOL1"
log_must is_pool_scrub_stopped "$TESTPOOL1"

# Resilver I/O must read the valid original child and repair the missing target.
log_must zinject -d "$SPARE_VDEV_FILE" -e io -T read -f 100 "$TESTPOOL1"
log_must zpool events -c
log_must zpool scrub -t "$TESTPOOL1"
log_must is_pool_scrubbing "$TESTPOOL1"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t scrub "$TESTPOOL1"
target_inject_count=$(inject_count) ||
    log_fail "zinject did not report exactly one active rule"
log_must zinject -c all
[[ "$target_inject_count" == 0 ]] ||
    log_fail "scan read the replacement target (inject=$target_inject_count)"
log_must is_pool_scrubbed "$TESTPOOL1"

pool_status=$(zpool status -P "$TESTPOOL1") ||
    log_fail "unable to read pool status after scrub"
[[ "$pool_status" != *replacing-* &&
    "$pool_status" != *"${VDEV_FILES[0]}"* &&
    "$pool_status" == *"$SPARE_VDEV_FILE"* ]] ||
    log_fail "replacement did not complete: $pool_status"

resilver_finish=$(zpool events | \
    awk '/sysevent.fs.zfs.resilver_finish/ { count++ } END { print count + 0 }')
[[ "$resilver_finish" == 1 ]] ||
    log_fail "expected one resilver finish event, found $resilver_finish"

last_scrubbed=$(zpool get -H -o value last_scrubbed_txg "$TESTPOOL1")
[[ "$last_scrubbed" == 0 ]] ||
    log_fail "DTL-limited scrub advanced last_scrubbed_txg to $last_scrubbed"

log_must check_pool_status "$TESTPOOL1" "scan" \
    "scrub repaired [1-9].*with 0 errors"
log_must check_pool_status "$TESTPOOL1" "errors" "No known data errors"
log_must check_pool_device "$TESTPOOL1" "$SPARE_VDEV_FILE" "0 *0 *0"
log_must zdb -cdui "$TESTPOOL1/$TESTFS"

log_pass "DTL-limited scrubs retain scrub controls and use resilver I/O"
