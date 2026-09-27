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

#
# DESCRIPTION:
#	Healing repair writes that fail on their first attempt, without a
#	retry, keep the replacement incomplete and its original attached.
#
# STRATEGY:
#	1. Replace the only device while failfast write errors suppress every
#	   write to the new device.
#	2. Require the healing pass to leave the replacement and its original.
#	3. Retry healing once the errors stop, then verify the file from the
#	   new device alone.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	destroy_pool "$TESTPOOL1"
	set_tunable32 SCAN_SUSPEND_PROGRESS "$orig_suspend"
	rm -rf "$workdir"
}

log_assert "Failfast repair write errors keep the replacement incomplete"
workdir=$(mktemp -d "$TEST_BASE_DIR/resilver_failfast.XXXXXX") ||
    log_fail "cannot create test directory"
orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
log_onexit cleanup
log_must truncate -s 512M "$workdir"/disk-{0,1}
log_must zpool create -f "$TESTPOOL1" "$workdir/disk-0"
log_must zfs create -o compression=off -o recordsize=128k "$TESTPOOL1/$TESTFS"
mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
log_must dd if=/dev/urandom of="$workdir/expected" bs=1M count=64
log_must cp "$workdir/expected" "$mntpnt/file"
sync_pool "$TESTPOOL1"

# Suppress the new device's writes and report a first-attempt EIO, which is
# not retried.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-1"
log_must zinject -F -d "$workdir/disk-1" -e noop -T write -f 100 "$TESTPOOL1"
log_must zinject -F -d "$workdir/disk-1" -e io -T write -f 100 "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
# A pass whose repairs failed is not retried until something changes.
log_must timeout 300 zpool wait -t resilver "$TESTPOOL1"
log_must zinject -c all
log_must is_pool_replacing "$TESTPOOL1"
log_must eval "zpool status '$TESTPOOL1' | grep -q '$workdir/disk-0'"

log_must zpool resilver "$TESTPOOL1"
log_must timeout 300 zpool wait -t resilver,replace "$TESTPOOL1"
log_mustnot is_pool_replacing "$TESTPOOL1"
log_must zpool export "$TESTPOOL1"
log_must rm "$workdir/disk-0"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$mntpnt/file"
log_must zpool scrub -w "$TESTPOOL1"
log_must check_pool_status "$TESTPOOL1" "errors" "No known data errors"

log_pass "Failfast repair write errors kept the replacement incomplete"
