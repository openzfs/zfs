#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Projected written-property lookup preserves legacy error handling by failing
# on a current snapshot deadlist error while omitting written when only the
# predecessor cannot be read. TXG-filtered snapshots still access the current
# deadlist but avoid accessing the predecessor.
#
# STRATEGY:
# 1. Create and export a real file-backed pool with two snapshots.
# 2. Open it through libzpool with a helper that injects EIO for one exact MOS
#    object at a time through a link-time wrapper.
# 3. Verify current-deadlist and predecessor behavior for unfiltered snapshots
#    and snapshots excluded by either TXG bound.
#

verify_runnable "global"

typeset ERROR_POOL="${TESTPOOL}_written_errors_$$"
typeset ERROR_DATASET="$ERROR_POOL/fs"
typeset ERROR_VDEV="$TEST_BASE_DIR/zfs_list_written_errors_vdev.$$"

function cleanup
{
	if ! poolexists "$ERROR_POOL"; then
		zpool import -d "$TEST_BASE_DIR" "$ERROR_POOL" >/dev/null 2>&1
	fi
	poolexists "$ERROR_POOL" && destroy_pool "$ERROR_POOL"
	rm -f "$ERROR_VDEV"
}

log_onexit cleanup
log_assert "Projected written lookup distinguishes deadlist errors."

log_must truncate -s "$MINVDEVSIZE" "$ERROR_VDEV"
log_must zpool create -f -o cachefile=none -O mountpoint=none \
    "$ERROR_POOL" "$ERROR_VDEV"
log_must zfs create -o mountpoint=none "$ERROR_DATASET"
log_must zfs snapshot "$ERROR_DATASET@first"
log_must zfs snapshot "$ERROR_DATASET@second"
log_must zpool sync "$ERROR_POOL"
log_must zpool export "$ERROR_POOL"

log_must snapshot_list_stats_test "$ERROR_DATASET@second" "$TEST_BASE_DIR"

log_must zpool import -d "$TEST_BASE_DIR" "$ERROR_POOL"
log_must destroy_pool "$ERROR_POOL"
log_must rm -f "$ERROR_VDEV"

log_pass "Projected written lookup distinguishes deadlist errors."
