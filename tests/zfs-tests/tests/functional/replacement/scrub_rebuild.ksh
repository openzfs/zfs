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
# Scrub reads overlapping a mirror or dRAID sequential rebuild do not report
# checksum errors for data the new device does not yet hold.
#
# STRATEGY:
# Keep a scrub active when sequential reconstruction starts. Verify that
# reconstruction takes priority and cancels the scrub, then complete a
# clean scrub of the rebuilt topology and verify its data.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL1
	log_must set_tunable32 SCAN_LEGACY $scan_legacy
	log_must set_tunable64 SCAN_VDEV_LIMIT $scan_limit
	log_must set_tunable32 SCRUB_MIN_TIME_MS $scrub_min_time
	log_must set_tunable32 REBUILD_SCRUB_ENABLED $rebuild_scrub
	rm -rf "$workdir"
}

function start_delayed_scrub
{
	typeset vdev=$1
	typeset delay=$2

	log_must zinject -d "$vdev" -D "$delay:1" -T read $TESTPOOL1
	log_must zpool scrub $TESTPOOL1
	log_must is_pool_scrubbing $TESTPOOL1
}

function verify_rebuild
{
	log_must zinject -c all
	log_must zpool wait -t resilver $TESTPOOL1
	log_must zpool wait -t scrub $TESTPOOL1
	log_must eval "zpool history -i $TESTPOOL1 | grep -q 'scan cancelled'"
	verify_pool $TESTPOOL1
}

log_assert "Scrub reads do not report errors on a device being rebuilt"
scan_legacy=$(get_tunable SCAN_LEGACY) || log_fail "cannot read SCAN_LEGACY"
scan_limit=$(get_tunable SCAN_VDEV_LIMIT) || log_fail "cannot read SCAN_VDEV_LIMIT"
scrub_min_time=$(get_tunable SCRUB_MIN_TIME_MS) ||
    log_fail "cannot read SCRUB_MIN_TIME_MS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_rebuild.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must set_tunable32 SCAN_LEGACY 1
log_must set_tunable64 SCAN_VDEV_LIMIT $((1024 * 1024))
log_must set_tunable32 SCRUB_MIN_TIME_MS 50
log_must set_tunable32 REBUILD_SCRUB_ENABLED 1

log_note "Ordinary mirror"
log_must truncate -s 512M "$workdir"/disk{0,1}
log_must zpool create -f -O compression=off -m "$workdir/mnt" \
    $TESTPOOL1 "$workdir/disk0"
log_must file_write -o create -f "$workdir/mnt/data" -b 1048576 -c 128 -d 65
log_must zpool sync $TESTPOOL1
start_delayed_scrub "$workdir/disk0" 10
log_must zpool attach -s $TESTPOOL1 "$workdir/disk0" "$workdir/disk1"
verify_rebuild

log_must zpool destroy $TESTPOOL1
log_must rm -f "$workdir"/disk*

log_note "Nested dRAID replacement"
log_must truncate -s 512M "$workdir"/disk{0..4} "$workdir/replacement"
log_must zpool create -f -O compression=off -m "$workdir/mnt" \
    $TESTPOOL1 draid1:2d:5c:1s "$workdir"/disk{0..4}
log_must file_write -o create -f "$workdir/expected" \
    -b 1048576 -c 128 -d 65
log_must cp "$workdir/expected" "$workdir/mnt/data"
log_must zpool sync $TESTPOOL1

log_must zpool replace -s $TESTPOOL1 "$workdir/disk2" draid1-0-0
log_must zpool wait -t resilver $TESTPOOL1
log_must zpool wait -t scrub $TESTPOOL1

start_delayed_scrub "$workdir/disk0" 50
log_must zpool replace -s $TESTPOOL1 \
    "$workdir/disk2" "$workdir/replacement"
verify_rebuild
log_must zpool export $TESTPOOL1
log_must zpool import -d "$workdir" $TESTPOOL1
log_must cmp "$workdir/expected" "$workdir/mnt/data"

log_pass "Scrub reads did not report errors on devices being rebuilt"
