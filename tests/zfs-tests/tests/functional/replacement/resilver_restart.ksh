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

# shellcheck disable=SC1091

. "$STF_SUITE"/include/libtest.shlib
. "$STF_SUITE"/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
#	Healing requests whose target disappears do not start a scrub.
#
# STRATEGY:
#	1. Leave a replacement incomplete by failing its repair writes.
#	2. Request another resilver, then detach its target.
#	3. Advance transaction groups past the request and check that no scrub
#	   was started.
#	4. Request healing without missing work and verify a paused scrub is
#	   preserved.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	# Destroy before restoring progress, including on a failed assertion.
	destroy_pool "$TESTPOOL1"
	set_tunable32 SCAN_SUSPEND_PROGRESS "$orig_suspend"
	rm -rf "$workdir"
}

function scrub_starts
{
	# func=1 is POOL_SCAN_SCRUB. History also catches a scrub that already
	# finished and would be missed by inspecting the current scan state.
	zpool history -i "$TESTPOOL1" |
	    awk '/ scan setup / && /func=1 / { n++ } END { print n + 0 }'
}

log_assert "Stale healing requests do not create or restart scrubs"

orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
workdir=$(mktemp -d "$TEST_BASE_DIR/resilver_restart.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must truncate -s 512M "$workdir"/disk-{0..1}
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool create -f "$TESTPOOL1" "$workdir/disk-0"
log_must zfs create "$TESTPOOL1/$TESTFS"
mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
log_must dd if=/dev/urandom of="$mntpnt/file" bs=1M count=8
sync_pool "$TESTPOOL1"
log_must zpool replace "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-1"
log_must zinject -d "$workdir/disk-1" -e io -T write -f 100 "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zinject -c all
log_must is_pool_replacing "$TESTPOOL1"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
before=$(scrub_starts)
log_must zpool resilver "$TESTPOOL1"
log_must zpool detach "$TESTPOOL1" "$workdir/disk-1"
# Drive both the async request and its syncing handoff after removing the work.
for ((i = 0; i < 8; i++)); do
	sync_pool "$TESTPOOL1"
done
log_must test "$(scrub_starts)" = "$before"
# A request already started before detach may finish as a resilver.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_mustnot is_pool_scrubbing "$TESTPOOL1"

# A no-work request must also preserve the identity and pause state of an
# existing scrub, rather than canceling it and creating another scan.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub "$TESTPOOL1"
log_must zpool scrub -p "$TESTPOOL1"
before=$(scrub_starts)
log_must zpool resilver "$TESTPOOL1"
sync_pool "$TESTPOOL1"
log_must zpool wait -t resilver "$TESTPOOL1"
log_must test "$(scrub_starts)" = "$before"
log_must is_pool_scrub_paused "$TESTPOOL1"
log_must zpool scrub -s "$TESTPOOL1"

log_pass "Stale healing requests do not create or restart scrubs"
