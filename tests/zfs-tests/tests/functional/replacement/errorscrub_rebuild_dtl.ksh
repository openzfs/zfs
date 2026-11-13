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

. "$STF_SUITE"/include/libtest.shlib

#
# DESCRIPTION:
# Post-rebuild verification checks pending errors older than a residual DTL.
#
# STRATEGY:
# Record a persistent error on one mirror before adding a second mirror.
# Defer an error scrub behind a sequential attach on the second mirror.
# Offline a child of the first mirror during later writes, then online it.
# The automatic scrub must check the older block as well as the missing writes.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$fault" ]] && log_must zinject -c "$fault"
	destroy_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	rm -rf "$workdir"
}

log_assert "Residual DTLs do not narrow post-rebuild error verification"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
workdir=$(mktemp -d "$TEST_BASE_DIR/errorscrub_rebuild_dtl.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must set_tunable32 REBUILD_SCRUB_ENABLED 1
log_must truncate -s 512M "$workdir"/disk{0..4}
log_must zpool create -f -O compression=off -O primarycache=metadata \
    -m "$workdir/mnt" "$TESTPOOL1" mirror "$workdir"/disk{0,1}
log_must file_write -o create -f "$workdir/mnt/older" \
    -b 131072 -c 1 -d 65
log_must zpool sync "$TESTPOOL1"
fault=$(zinject -q -t data -e checksum -f 100 -a "$workdir/mnt/older") ||
    log_fail "cannot inject read errors"
log_mustnot cat "$workdir/mnt/older" >/dev/null
log_must zpool sync "$TESTPOOL1"
log_must eval "zpool status -v $TESTPOOL1 | grep -q '$workdir/mnt/older'"

log_must zpool add "$TESTPOOL1" mirror "$workdir"/disk{2,3}
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s "$TESTPOOL1" "$workdir/disk2" "$workdir/disk4"
log_must is_pool_resilvering "$TESTPOOL1"
log_must zpool scrub -e "$TESTPOOL1"
log_must zpool scrub -p "$TESTPOOL1"
log_must is_pool_error_scrub_paused "$TESTPOOL1"
log_must zpool offline "$TESTPOOL1" "$workdir/disk0"
log_must file_write -o create -f "$workdir/mnt/later" \
    -b 1048576 -c 16 -d 66
log_must zpool sync "$TESTPOOL1"
log_must zpool online "$TESTPOOL1" "$workdir/disk0"
log_must zpool sync "$TESTPOOL1"
log_must zpool sync "$TESTPOOL1"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver,scrub "$TESTPOOL1"
log_must zpool sync "$TESTPOOL1"
log_must zpool status -v "$TESTPOOL1"
log_must eval \
    "zpool status $TESTPOOL1 | grep -Eq 'scrub repaired .* with [1-9][0-9]* errors'"
log_must eval "zpool status -v $TESTPOOL1 | grep -q '$workdir/mnt/older'"

log_pass "The automatic scrub revisited the older error despite residual DTLs"
