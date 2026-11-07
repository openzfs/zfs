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
# Requested scrub bounds preserve unexamined DTLs and clear covered ones.
#
# STRATEGY:
# Record a healing write failure on an attached data device with separate
# metadata storage. An incremental scrub must retain the older DTL entries;
# a bounded scrub ending before attachment must clear them.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$fault" ]] && log_must zinject -c "$fault"
	destroy_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	rm -rf "$workdir"
}

log_assert "Scrub bounds preserve unexamined DTLs and clear covered ones"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_dtl.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must truncate -s 512M "$workdir"/disk{0..2}
log_must zpool create -f -O compression=off \
    -m "$workdir/mnt" "$TESTPOOL1" "$workdir/disk0" \
    special "$workdir/disk2"
log_must file_write -o create -f "$workdir/mnt/data" \
    -b 131072 -c 1 -d 65
log_must zpool sync "$TESTPOOL1"

# The end date includes its whole minute. Record the following boundary
# before attachment; export/import records it in the time-to-TXG log.
end=$(date '+%Y-%m-%d %H:%M')
seconds=$(date +%S)
log_must sleep $((60 - 10#$seconds))
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must zpool scrub -w "$TESTPOOL1"
cutoff=$(get_pool_prop last_scrubbed_txg "$TESTPOOL1")
log_must test "$cutoff" -gt 0
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach "$TESTPOOL1" "$workdir/disk0" "$workdir/disk1"
fault=$(zinject -q -d "$workdir/disk1" -e io -T write -f 100 "$TESTPOOL1") ||
    log_fail "cannot inject a healing write failure"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zinject -c "$fault"
fault=

# Separate metadata storage keeps the residual DTL confined to old data.
marker=$(zdb -C "$TESTPOOL1" | awk '/resilver_txg:/ { print $2 }')
log_must test "$marker" -gt 0
pause_scrub "$TESTPOOL1" -C
actual=$(zpool history -i "$TESTPOOL1" |
    sed -n 's/.*scan setup func=1 mintxg=\([0-9]*\) .*/\1/p' | tail -1)
log_must test "$actual" = "$cutoff"
log_must zpool scrub -w "$TESTPOOL1"
remaining=$(zdb -C "$TESTPOOL1" | awk '/resilver_txg:/ { print $2 }')
log_must test "$remaining" = "$marker"

pause_scrub "$TESTPOOL1" -E "$end"
upper=$(zpool history -i "$TESTPOOL1" |
    sed -n 's/.*scan setup func=1 .*maxtxg=\([0-9]*\).*/\1/p' | tail -1)
log_must test "$upper" -gt 0
log_must test "$upper" -lt "$marker"
log_must zpool scrub -w "$TESTPOOL1"
log_must check_pool_status "$TESTPOOL1" scan \
    "scrub repaired .* with 0 errors" true
marker=$(zdb -C "$TESTPOOL1" | awk '/resilver_txg:/ { print $2 }')
log_must test -z "$marker"
log_pass "Scrub bounds preserve unexamined DTLs and clear covered ones"
