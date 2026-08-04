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
# 2. Open it through libzpool with a shim that injects EIO for one exact MOS
#    object at a time.
# 3. Verify current-deadlist and predecessor behavior for unfiltered snapshots
#    and snapshots excluded by either TXG bound.
#

verify_runnable "global"

typeset ERROR_POOL="${TESTPOOL}_written_errors_$$"
typeset ERROR_DATASET="$ERROR_POOL/fs"
typeset ERROR_VDEV="$TEST_BASE_DIR/zfs_list_written_errors_vdev.$$"
typeset MARKER="$TEST_BASE_DIR/zfs_list_written_errors_marker.$$"

function cleanup
{
	rm -f "$MARKER"
	if ! poolexists "$ERROR_POOL"; then
		zpool import -d "$TEST_BASE_DIR" "$ERROR_POOL" >/dev/null 2>&1
	fi
	poolexists "$ERROR_POOL" && destroy_pool "$ERROR_POOL"
	rm -f "$ERROR_VDEV"
}

function find_shim
{
	typeset helper helper_dir candidate

	helper=$(readlink -f "$(command -v snapshot_list_test)")
	helper_dir=${helper%/*}
	for candidate in \
	    "$helper_dir/.libs/libsnapshot_list_test_shim.so" \
	    "$helper_dir/libsnapshot_list_test_shim.so" \
	    "$STF_SUITE/bin/libsnapshot_list_test_shim.so"; do
		[[ -f "$candidate" ]] && print -- "$candidate" && return 0
	done
	return 1
}

log_onexit cleanup
log_assert "Projected written lookup distinguishes deadlist errors."

SHIM=$(find_shim) || log_unsupported "snapshot-list test shim not found"
typeset preload="$SHIM"
[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"

log_must truncate -s "$MINVDEVSIZE" "$ERROR_VDEV"
log_must zpool create -f -o cachefile=none -O mountpoint=none \
    "$ERROR_POOL" "$ERROR_VDEV"
log_must zfs create -o mountpoint=none "$ERROR_DATASET"
log_must zfs snapshot "$ERROR_DATASET@first"
log_must zfs snapshot "$ERROR_DATASET@second"
log_must zpool sync "$ERROR_POOL"
log_must zpool export "$ERROR_POOL"

log_must env LD_PRELOAD="$preload" snapshot_list_stats_test \
    "$ERROR_DATASET@second" "$TEST_BASE_DIR" "$MARKER"

log_must zpool import -d "$TEST_BASE_DIR" "$ERROR_POOL"
log_must destroy_pool "$ERROR_POOL"
log_must rm -f "$ERROR_VDEV"

log_pass "Projected written lookup distinguishes deadlist errors."
