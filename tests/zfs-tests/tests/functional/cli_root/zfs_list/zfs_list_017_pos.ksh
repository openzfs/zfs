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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Batched snapshot iteration delivers, from the failing ioctl, results
# collected before a metadata EIO and matches legacy iteration.
#
# STRATEGY:
# 1. Create three snapshots in an isolated file-backed pool.
# 2. Inject EIO into the user-reference ZAP of a late snapshot.
# 3. Verify the failing batched ioctl itself contains snapshot results.
# 4. Verify legacy and batched iteration return the same callbacks before EIO.
#

verify_runnable "global"
set -o pipefail

typeset ERROR_POOL="${TESTPOOL}_partial_eio_$$"
typeset ERROR_DATASET="$ERROR_POOL/fs"
typeset ERROR_VDEV="$TEST_BASE_DIR/zfs_list_partial_eio_vdev.$$"
typeset ORDER="$TEST_BASE_DIR/zfs_list_partial_eio_order.$$"
typeset MARKER="$TEST_BASE_DIR/zfs_list_partial_eio_marker.$$"
typeset HANDLER=
typeset PRELOAD
typeset USERREFS_OBJ
typeset USERREFS_HEX
typeset TARGET
typeset -i EXPECTED_CALLBACKS

function cleanup
{
	[[ -n "$HANDLER" ]] && zinject -c "$HANDLER" >/dev/null 2>&1
	[[ -e "$TEST_BASE_DIR/tunable-SPA_LOAD_VERIFY_METADATA" ]] &&
	    restore_tunable SPA_LOAD_VERIFY_METADATA >/dev/null 2>&1
	[[ -e "$TEST_BASE_DIR/tunable-SNAPSHOT_LIST_BATCH_TIME_US" ]] &&
	    restore_tunable SNAPSHOT_LIST_BATCH_TIME_US >/dev/null 2>&1
	[[ -e "$TEST_BASE_DIR/tunable-SNAPSHOT_LIST_BATCH_SIZE" ]] &&
	    restore_tunable SNAPSHOT_LIST_BATCH_SIZE >/dev/null 2>&1
	if ! poolexists "$ERROR_POOL"; then
		zpool import -d "$TEST_BASE_DIR" "$ERROR_POOL" >/dev/null 2>&1
	fi
	poolexists "$ERROR_POOL" && destroy_pool "$ERROR_POOL"
	rm -f "$ERROR_VDEV" "$ORDER" "$MARKER"
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

function verify_partial_error
{
	typeset mode="$1"
	typeset injected

	HANDLER=$(zinject -q -a -u -b "0:$USERREFS_HEX:0:0" \
	    "$ERROR_POOL") || log_fail "cannot inject $mode iterator EIO"
	[[ "$HANDLER" == +([0-9]) ]] ||
	    log_fail "invalid zinject handler: $HANDLER"

	if [[ "$mode" == "batched" ]]; then
		log_must rm -f "$MARKER"
		log_must env LD_PRELOAD="$PRELOAD" \
		    ZFS_SNAPSHOT_LIST_TEST_MODE=partial_eio_output \
		    ZFS_SNAPSHOT_LIST_TEST_MARKER="$MARKER" \
		    snapshot_list_test partial-error "$ERROR_DATASET" "$mode" \
		    "$EXPECTED_CALLBACKS"
		log_must grep -Fxq partial_eio_output "$MARKER"
	else
		log_must snapshot_list_test partial-error "$ERROR_DATASET" \
		    "$mode" "$EXPECTED_CALLBACKS"
	fi
	injected=$(zinject | awk -v id="$HANDLER" \
	    '$1 == id { print $NF }')
	[[ -n "$injected" ]] && ((injected > 0)) ||
	    log_fail "$mode iterator did not trigger EIO"

	log_must zinject -c "$HANDLER"
	HANDLER=
}

log_onexit cleanup
log_assert "Batched iteration returns snapshots collected before EIO."

SHIM=$(find_shim) || log_unsupported "snapshot-list test shim not found"
PRELOAD="$SHIM"
[[ -n "$LD_PRELOAD" ]] && PRELOAD="$SHIM:$LD_PRELOAD"

log_must save_tunable SPA_LOAD_VERIFY_METADATA
log_must save_tunable SNAPSHOT_LIST_BATCH_SIZE
log_must save_tunable SNAPSHOT_LIST_BATCH_TIME_US
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1024
log_must set_tunable32 SNAPSHOT_LIST_BATCH_TIME_US 100000
log_must truncate -s "$MINVDEVSIZE" "$ERROR_VDEV"
log_must zpool create -f -o cachefile=none -O mountpoint=none \
    "$ERROR_POOL" "$ERROR_VDEV"
log_must zfs create -o mountpoint=none "$ERROR_DATASET"
log_must zfs snapshot "$ERROR_DATASET@first"
log_must zfs snapshot "$ERROR_DATASET@second"
log_must zfs snapshot "$ERROR_DATASET@latest"
log_must eval "snapshot_list_test filter '$ERROR_DATASET' 0 0 > '$ORDER'"

TARGET=$(awk -v latest="$ERROR_DATASET@latest" \
    '$0 != latest { target=$0 } END { print target }' "$ORDER")
EXPECTED_CALLBACKS=$(awk -v target="$TARGET" \
    '$0 == target { print NR - 1; exit }' "$ORDER")
[[ -n "$TARGET" ]] && ((EXPECTED_CALLBACKS > 0)) ||
    log_fail "cannot select a late non-latest snapshot"

typeset -i i=0
while ((i < 40)); do
	log_must zfs hold "partial-eio-$i-abcdefghijklmnopqrstuvwxyz" "$TARGET"
	((i += 1))
done
log_must zpool sync "$ERROR_POOL"

typeset dsobj=$(zfs get -H -o value objsetid "$TARGET")
log_must zpool export "$ERROR_POOL"
USERREFS_OBJ=$(zdb -e -p "$TEST_BASE_DIR" -dddd "$ERROR_POOL" \
    "$dsobj" 2>/dev/null | awk '$1 == "userrefs_obj" { print $3 }')
[[ -n "$USERREFS_OBJ" ]] && ((USERREFS_OBJ > 0)) ||
    log_fail "cannot find user-reference ZAP for $TARGET"
USERREFS_HEX=$(printf "%x" "$USERREFS_OBJ")
log_must zpool import -d "$TEST_BASE_DIR" "$ERROR_POOL"

log_must set_tunable32 SPA_LOAD_VERIFY_METADATA 0
verify_partial_error legacy
verify_partial_error batched
log_must restore_tunable SPA_LOAD_VERIFY_METADATA
log_must restore_tunable SNAPSHOT_LIST_BATCH_TIME_US
log_must restore_tunable SNAPSHOT_LIST_BATCH_SIZE

log_must destroy_pool "$ERROR_POOL"
log_must rm -f "$ERROR_VDEV" "$ORDER" "$MARKER"

log_pass "Batched iteration returns snapshots collected before EIO."
