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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# The first scrub after a sequential rebuild repairs data still missing from
# an online replacement before the original is detached.
#
# STRATEGY:
# 1. Hold a dRAID rebuild and fail writes to the replacement.
# 2. Rewrite a file until the rebuild completes. The completion runs after
#    the data writes of its txg, so the failed writes of that txg remain in
#    the replacement's DTL. Failing writes only while the rebuild is held
#    leaves none: each later metaslab extends the rebuild's range to the
#    current DTLs.
# 3. Write the checked data with a new pattern, since reused space on the
#    replacement may hold an earlier version copied by the rebuild.
# 4. Verify that the original remains until the first scrub, and that no
#    other healing pass precedes its detachment.
# 5. Offline another dRAID child and verify the data after export/import,
#    which requires the first scrub to have repaired the replacement.
#

verify_runnable "global"

function cleanup
{
	if [[ -n "$writer" ]]; then
		touch "$workdir/stop"
		wait $writer
	fi
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	log_must restore_tunable SCAN_SUSPEND_PROGRESS
	log_must restore_tunable REBUILD_SCRUB_ENABLED
	rm -rf "$workdir"
}

function write_late
{
	file_write -o create -f "$workdir/mnt/late" -b 1048576 -c 1 -d 66
}

# Release the held rebuild once failed writes are under way, and keep them
# coming until the rebuild completes.
function rewrite_late
{
	write_late
	typeset -i rc=$?
	set_tunable32 SCAN_SUSPEND_PROGRESS 0 || return
	(( rc == 0 )) || return $rc
	while [[ ! -e "$workdir/stop" ]]; do
		write_late || return
	done
}

log_assert "The first scrub repairs residual sequential rebuild DTLs"
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_rebuild_residual.XXXXXX") ||
    log_fail "cannot create test directory"
log_must save_tunable REBUILD_SCRUB_ENABLED
log_must save_tunable SCAN_SUSPEND_PROGRESS
log_onexit cleanup
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

# Every block of a dRAID group as wide as the vdev writes every child.
log_must truncate -s 512M "$workdir"/disk{0..3} "$workdir/replacement"
log_must zpool create -f -O compression=off -m "$workdir/mnt" \
    $TESTPOOL1 draid1:3d:4c:0s "$workdir"/disk{0..3}
log_must file_write -o create -f "$workdir/expected" \
    -b 1048576 -c 1 -d 67

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s $TESTPOOL1 \
    "$workdir/disk2" "$workdir/replacement"
log_must is_pool_resilvering $TESTPOOL1
log_must zinject -d "$workdir/replacement" \
    -e io-prefail -T write -f 100 $TESTPOOL1
rewrite_late &
writer=$!
log_must zpool wait -t resilver $TESTPOOL1
log_must touch "$workdir/stop"
wait $writer || log_fail "rewriting the late file failed"
writer=
log_must cp "$workdir/expected" "$workdir/mnt/late"
log_must zpool sync $TESTPOOL1
matches=$(zinject | awk '$4 == "write" && $5 == "io-prefail" {
    total += $(NF - 1) } END { print total + 0 }')
(( matches > 0 )) || log_fail "replacement write failure did not fire"
log_must zinject -c all

log_must check_vdev_state $TESTPOOL1 "$workdir/disk2" ONLINE
log_must check_vdev_state $TESTPOOL1 "$workdir/replacement" ONLINE

log_must zpool scrub -w $TESTPOOL1
log_must zpool wait -t replace $TESTPOOL1
log_must check_pool_status $TESTPOOL1 scan \
    "scrub repaired .* with 0 errors" true
log_must test -z "$(get_device_state $TESTPOOL1 "$workdir/disk2")"
log_must zpool offline $TESTPOOL1 "$workdir/disk0"
log_must zpool export $TESTPOOL1
log_must zpool import -d "$workdir" $TESTPOOL1
log_must cmp "$workdir/expected" "$workdir/mnt/late"

log_pass "The first scrub repaired residual sequential rebuild DTLs"
