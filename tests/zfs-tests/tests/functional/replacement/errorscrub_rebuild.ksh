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
# Full verification supersedes an error scrub after sequential reconstruction.
#
# STRATEGY:
# Record transient read errors, clear the injection and import to reset device
# error counters. Hold a sequential rebuild, request an error scrub and release
# both. Verify the automatic full scrub completes without checksum errors and
# read the data with the original offline.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$fault" ]] && log_must zinject -c "$fault"
	[[ -n "$delay_fault" ]] && log_must zinject -c "$delay_fault"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	destroy_pool "$TESTPOOL1"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	rm -rf "$workdir"
}

log_assert "An error scrub does not prevent automatic full verification"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
workdir=$(mktemp -d "$TEST_BASE_DIR/errorscrub_rebuild.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must set_tunable32 REBUILD_SCRUB_ENABLED 1

log_must truncate -s 512M "$workdir"/disk{0,1}
log_must zpool create -f -O compression=off -O primarycache=metadata \
    -m "$workdir/mnt" "$TESTPOOL1" "$workdir/disk0"
log_must file_write -o create -f "$workdir/expected" -b 131072 -c 1 -d 65
log_must cp "$workdir/expected" "$workdir/mnt/data"
log_must zpool sync "$TESTPOOL1"

fault=$(zinject -q -t data -e checksum -f 100 -a "$workdir/mnt/data") ||
    log_fail "cannot inject read errors"
log_mustnot cat "$workdir/mnt/data" >/dev/null
log_must zinject -c "$fault"
fault=
log_must zpool sync "$TESTPOOL1"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s "$TESTPOOL1" "$workdir/disk0" "$workdir/disk1"
log_must is_pool_resilvering "$TESTPOOL1"
delay_fault=$(zinject -q -d "$workdir/disk0" -D 50:1 -T read "$TESTPOOL1") ||
    log_fail "cannot delay rebuild reads"
log_must zpool scrub -e "$TESTPOOL1"
log_must is_pool_error_scrubbing "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zinject -c "$delay_fault"
delay_fault=
# No manual scrub may supply the automatic full verification.
log_must zpool wait -t scrub "$TESTPOOL1"
verify_pool_scrubbed "$TESTPOOL1"
log_must zpool offline "$TESTPOOL1" "$workdir/disk0"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$workdir/mnt/data"

log_pass "The automatic full scrub verified the completed replacement"
