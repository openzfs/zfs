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
# DDT class changes cannot issue scrub reads against an incomplete rebuild.
#
# STRATEGY:
# Pause a legacy scrub with a unique deduplicated block. Give that block a
# second reference in the transaction that starts sequential reconstruction.
# Verify cancellation, the unique-to-duplicate transition, automatic full
# verification, and cold data read from the replacement.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	destroy_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_LEGACY "$scan_legacy"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	log_must set_tunable32 TXG_TIMEOUT "$txg_timeout"
	rm -rf "$workdir"
}

log_assert "DDT class changes cannot scan an incomplete replacement"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
scan_legacy=$(get_tunable SCAN_LEGACY) ||
    log_fail "cannot read SCAN_LEGACY"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
txg_timeout=$(get_tunable TXG_TIMEOUT) || log_fail "cannot read TXG_TIMEOUT"
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_rebuild_ddt.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must set_tunable32 SCAN_LEGACY 1
log_must set_tunable32 REBUILD_SCRUB_ENABLED 1
log_must set_tunable32 TXG_TIMEOUT 600
log_must truncate -s 512M "$workdir"/disk{0,1}
log_must zpool create -f -o feature@fast_dedup=disabled \
    -O dedup=on -O compression=off -O primarycache=metadata -O xattr=sa \
    -m "$workdir/mnt" "$TESTPOOL1" "$workdir/disk0"
log_must file_write -o create -f "$workdir/expected" -b 131072 -c 1 -d 65
log_must cp "$workdir/expected" "$workdir/mnt/first"
log_must zpool sync "$TESTPOOL1"
log_must eval "zdb -D $TESTPOOL1 | grep -q 'DDT-sha256-zap-unique:.*entries=1$'"

pause_scrub "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool sync "$TESTPOOL1"
# Read from outside the pool so block cloning cannot bypass the DDT.
log_must cp "$workdir/expected" "$workdir/mnt/second"
log_must zpool attach -s "$TESTPOOL1" "$workdir/disk0" "$workdir/disk1"
log_must is_pool_resilvering "$TESTPOOL1"
log_must eval "zpool history -i $TESTPOOL1 | grep -q 'scan cancelled'"
ddt=$(zdb -DDD "$TESTPOOL1") || log_fail "cannot inspect the DDT"
log_must eval \
    "echo '$ddt' | grep -q 'DDT-sha256-zap-duplicate:.*entries=1$'"

# The new duplicate-class ZAP is written only by this transition. Its birth,
# unlike the deduplicated data block's birth, identifies the transition TXG.
duplicate_obj=$(echo "$ddt" |
    sed -n 's/^DDT-sha256-zap-duplicate:.*object=//p')
log_must test -n "$duplicate_obj"
duplicate_txg=$(zdb -dddddd "$TESTPOOL1" "$duplicate_obj" |
    awk '$1 == "0" && $2 == "L0" {
	for (i = 3; i <= NF; i++)
		if ($i ~ /^B=/) {
			split(substr($i, 3), birth, "/")
			print birth[1]
		}
    }')
rebuild_txg=$(zpool history -i "$TESTPOOL1" |
    sed -n 's/.*\[txg:\([0-9]*\)\] rebuild .* started$/\1/p')
log_must test -n "$rebuild_txg"
log_must test "$duplicate_txg" = "$rebuild_txg"

log_must set_tunable32 TXG_TIMEOUT "$txg_timeout"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool sync "$TESTPOOL1"
log_must zpool wait -t resilver,scrub "$TESTPOOL1"
verify_pool_scrubbed "$TESTPOOL1"
log_must zpool offline "$TESTPOOL1" "$workdir/disk0"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$workdir/mnt/first"
log_must cmp "$workdir/expected" "$workdir/mnt/second"

log_pass "DDT class changes preserved safe, complete scrub verification"
