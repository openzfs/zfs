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
# Healing cannot consume the full verification required after a rebuild.
#
# STRATEGY:
# Pause the automatic scrub after sequential reconstruction, then make
# another mirror miss writes and request healing. Export and import with
# healing held, then require a completed full scrub and cold data comparison.
# A standalone resilver wait must cover the handoff TXG's label writes.
# After completed and canceled verification, require later healing to finish
# without starting another scrub.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$fault" ]] && log_must zinject -c "$fault"
	[[ -n "$write_fault" ]] && log_must zinject -c "$write_fault"
	destroy_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	log_must set_tunable32 SCAN_LEGACY "$scan_legacy"
	log_must set_tunable64 SCAN_VDEV_LIMIT "$scan_limit"
	log_must set_tunable32 SCRUB_MIN_TIME_MS "$scrub_min_time"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	rm -rf "$workdir"
}

function verify_ordinary_healing
{
	log_must file_write -o create -f "$workdir/mnt/healing" \
	    -b 1048576 -c 16 -d 67
	log_must zpool sync "$TESTPOOL1"
	log_must zpool online "$TESTPOOL1" "$workdir/disk1"
	log_must zpool resilver "$TESTPOOL1"
	log_must zpool sync "$TESTPOOL1"
	log_must zpool wait -t resilver,scrub "$TESTPOOL1"
	log_must is_pool_resilvered "$TESTPOOL1" true
}

log_assert "Post-rebuild verification survives healing until completed or canceled"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
scan_legacy=$(get_tunable SCAN_LEGACY) || log_fail "cannot read SCAN_LEGACY"
scan_limit=$(get_tunable SCAN_VDEV_LIMIT) || log_fail "cannot read SCAN_VDEV_LIMIT"
scrub_min_time=$(get_tunable SCRUB_MIN_TIME_MS) ||
    log_fail "cannot read SCRUB_MIN_TIME_MS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_rebuild_resilver.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must set_tunable32 SCAN_LEGACY 1
log_must set_tunable64 SCAN_VDEV_LIMIT $((1024 * 1024))
log_must set_tunable32 SCRUB_MIN_TIME_MS 50
log_must set_tunable32 REBUILD_SCRUB_ENABLED 1

log_must truncate -s 512M "$workdir"/disk{0..4}
log_must zpool create -f -O compression=off -O primarycache=metadata \
    -m "$workdir/mnt" "$TESTPOOL1" mirror "$workdir"/disk{0,1}
log_must file_write -o create -f "$workdir/mnt/data" \
    -b 1048576 -c 64 -d 65
log_must zpool sync "$TESTPOOL1"
log_must zpool add "$TESTPOOL1" mirror "$workdir"/disk{2,3}
fault=$(zinject -q -d "$workdir/disk0" -D 5:1 -T read "$TESTPOOL1") ||
    log_fail "cannot delay scrub reads"
log_must zpool attach -s "$TESTPOOL1" "$workdir/disk2" "$workdir/disk4"
log_must zpool wait -t resilver "$TESTPOOL1"
log_must is_pool_scrubbing "$TESTPOOL1"
log_must zpool scrub -p "$TESTPOOL1"
log_must is_pool_scrub_paused "$TESTPOOL1"
log_must zinject -c "$fault"
fault=

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool offline "$TESTPOOL1" "$workdir/disk0"
log_must file_write -o create -f "$workdir/expected" \
    -b 1048576 -c 16 -d 66
log_must cp "$workdir/expected" "$workdir/mnt/later"
log_must zpool sync "$TESTPOOL1"
log_must zpool online "$TESTPOOL1" "$workdir/disk0"
log_must zpool resilver "$TESTPOOL1"
log_must zpool sync "$TESTPOOL1"
log_must is_pool_resilvering "$TESTPOOL1"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must is_pool_resilvering "$TESTPOOL1"

fault=$(zinject -q -d "$workdir/disk0" -D 5:1 -T read "$TESTPOOL1") ||
    log_fail "cannot delay verification reads"
write_fault=$(zinject -q -d "$workdir/disk4" -D 100:1 -T write "$TESTPOOL1") ||
    log_fail "cannot delay completion writes"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must is_pool_scrubbing "$TESTPOOL1"

# Read labels before any command that could wait for another TXG.
labels=$(zdb -lu "$workdir/disk4") || log_fail "cannot read completion labels"
committed=$(echo "$labels" | awk '/txg =/ {
    if ($3 > max) max = $3 } END { print max + 0 }')
handoff=$(zpool history -i "$TESTPOOL1" |
    sed -n 's/.*\[txg:\([0-9]*\)\] scan setup func=1 .*/\1/p' | tail -1)
log_must test -n "$handoff"
log_must test "$committed" -ge "$handoff"
log_must zinject -c "$write_fault"
write_fault=
log_must zinject -c "$fault"
fault=
log_must zpool wait -t scrub "$TESTPOOL1"
verify_pool_scrubbed "$TESTPOOL1"
log_must zpool offline "$TESTPOOL1" "$workdir/disk1"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$workdir/mnt/later"

log_note "Completed verification does not affect later healing"
verify_ordinary_healing

log_note "Canceled automatic verification does not affect later healing"
log_must zpool detach "$TESTPOOL1" "$workdir/disk4"
fault=$(zinject -q -d "$workdir/disk0" -D 5:1 -T read "$TESTPOOL1") ||
    log_fail "cannot delay scrub reads"
log_must zpool attach -s "$TESTPOOL1" "$workdir/disk2" "$workdir/disk4"
log_must zpool wait -t resilver "$TESTPOOL1"
log_must is_pool_scrubbing "$TESTPOOL1"
log_must zpool scrub -s "$TESTPOOL1"
log_must is_pool_scrub_stopped "$TESTPOOL1"
log_must zinject -c "$fault"
fault=
log_must zpool offline "$TESTPOOL1" "$workdir/disk1"
verify_ordinary_healing

log_pass "Verification survives healing and import until completed or canceled"
