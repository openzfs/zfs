#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	"zpool set feature@<name>=disabled" first marks the feature as
#	disabling, and removes it from disk only once the transaction group
#	of the mark and the next two have synced.  Meanwhile the feature
#	reads as disabled and nothing new can use it, and enabling it again
#	ends the disable, which then fails.
#
# STRATEGY:
#	1. Delay the pool's writes and keep writing to it, so that each
#	   transaction group takes seconds to sync.
#	2. Disable zstd_compress in the background.  Once it reads as
#	   disabled, check that the disable is still running and that
#	   "zfs set compression=zstd" fails.
#	3. Enable zstd_compress while the disable runs: the disable must
#	   fail, and the feature read as enabled, with no disable in the
#	   pool history.
#	4. Disable it again without interference: it must read as disabled
#	   once the command returns.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/feature_window.vdev
OUT=$TEST_BASE_DIR/feature_window.out

function cleanup
{
	[[ -n "$writer" ]] && kill $writer 2>/dev/null
	[[ -n "$disabler" ]] && wait $disabler
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	rm -f $VDEV $OUT
}

function check_prop # feature value
{
	typeset val=$(get_pool_prop feature@$1 $TESTPOOL1)

	[[ "$val" == "$2" ]] ||
	    log_fail "feature@$1 is '$val', expected '$2'"
}

function running # pid
{
	kill -0 $1 2>/dev/null || {
		cat $OUT
		log_fail "the disable ended before the transaction groups synced"
	}
}

log_assert "A disable ends only once its transaction group and the next" \
    "two have synced, and enabling the feature meanwhile ends it"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -f $TESTPOOL1 $VDEV
log_must zfs create $TESTPOOL1/fs
check_prop zstd_compress enabled
mntpnt=$(get_prop mountpoint $TESTPOOL1/fs)

# 1. Slow transaction groups.
log_must zinject -d $VDEV -D 200:1 $TESTPOOL1
while :; do
	dd if=/dev/urandom of=$mntpnt/file bs=128k count=1 conv=notrunc \
	    >/dev/null 2>&1
	sleep 0.2
done &
writer=$!

# 2. The feature reads as disabled while the disable runs.
zpool set feature@zstd_compress=disabled $TESTPOOL1 >$OUT 2>&1 &
disabler=$!
for i in $(seq 600); do
	[[ $(get_pool_prop feature@zstd_compress $TESTPOOL1) == disabled ]] &&
	    break
	kill -0 $disabler 2>/dev/null || break
	sleep 0.1
done
check_prop zstd_compress disabled
running $disabler
log_mustnot zfs set compression=zstd $TESTPOOL1/fs
running $disabler

# 3. Enabling it again ends the disable.
log_must zpool set feature@zstd_compress=enabled $TESTPOOL1
wait $disabler
rc=$?
disabler=""
cat $OUT
(( rc != 0 )) || log_fail "the disable succeeded although the feature" \
    "was enabled again"
check_prop zstd_compress enabled
log_mustnot eval "zpool history $TESTPOOL1 | \
    grep -q 'feature@zstd_compress=disabled'"

# 4. Without interference, the disable succeeds.
kill $writer
writer=""
log_must zinject -c all
log_must zpool set feature@zstd_compress=disabled $TESTPOOL1
check_prop zstd_compress disabled
log_must eval "zpool history $TESTPOOL1 | \
    grep -q 'feature@zstd_compress=disabled'"
log_mustnot zfs set compression=zstd $TESTPOOL1/fs

log_pass "A disable ends only once its transaction group and the next" \
    "two have synced, and enabling the feature meanwhile ends it"
