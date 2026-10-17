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
# An incremental scrub retains its requested bounds and older missing writes.
#
# STRATEGY:
# Scrub a mirror while one child is offline with missing data. Hold a rebuild
# on a second mirror while onlining the first child, so healing cannot start.
# Complete reconstruction without an automatic scrub, then scrub from the
# previous cutoff. Older missing writes must remain eligible for healing.
#

verify_runnable "global"

function cleanup
{
	if poolexists "$TESTPOOL1"; then
		log_must zpool online "$TESTPOOL1" "$workdir/disk1"
	fi
	destroy_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	rm -rf "$workdir"
}

log_assert "Incremental scrub bounds do not discard older missing writes"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_dtl.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must truncate -s 512M "$workdir"/disk{0..4}
log_must zpool create -f -o failmode=continue -O compression=off \
    -m "$workdir/mnt" "$TESTPOOL1" mirror "$workdir"/disk{0,1}
log_must zpool offline "$TESTPOOL1" "$workdir/disk0"
log_must file_write -o create -f "$workdir/expected" \
    -b 1048576 -c 16 -d 65
log_must cp "$workdir/expected" "$workdir/mnt/data"
log_must zpool sync "$TESTPOOL1"
log_must zpool scrub -w "$TESTPOOL1"
cutoff=$(get_pool_prop last_scrubbed_txg "$TESTPOOL1")
log_must test "$cutoff" -gt 0

log_must zpool add "$TESTPOOL1" mirror "$workdir"/disk{2,3}
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s "$TESTPOOL1" "$workdir/disk2" "$workdir/disk4"
log_must is_pool_resilvering "$TESTPOOL1"
log_must zpool online "$TESTPOOL1" "$workdir/disk0"
log_must zpool sync "$TESTPOOL1"
log_must zpool sync "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub -C "$TESTPOOL1"
log_must zpool scrub -p "$TESTPOOL1"
actual=$(zpool history -i "$TESTPOOL1" |
    sed -n 's/.*scan setup func=1 mintxg=\([0-9]*\) .*/\1/p' | tail -1)
log_must test "$actual" = "$cutoff"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool scrub -w "$TESTPOOL1"
log_must zpool resilver "$TESTPOOL1"
log_must zpool sync "$TESTPOOL1"
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zpool offline "$TESTPOOL1" "$workdir/disk1"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$workdir/mnt/data"

log_pass "Incremental scrubbing preserved older data for healing"
