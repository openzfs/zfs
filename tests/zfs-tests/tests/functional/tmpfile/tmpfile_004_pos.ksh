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

. "$STF_SUITE/include/libtest.shlib"

if [[ "$(uname -s)" != "Linux" ]]; then
        log_skip "O_TMPFILE is not supported on this platform."
fi

verify_runnable "global"

log_assert "O_TMPFILE linking with xattr=sa should survive replay"

log_must zfs set xattr=sa "$TESTPOOL/$TESTFS"

typeset target_uid target_gid
if [[ "$(id -u)" -eq 0 ]]; then
        target_uid=1000
        target_gid=1001
else
        target_uid=$(id -u)
        target_gid=$(id -g)
fi

log_must test -n "$target_uid" -a -n "$target_gid"

export TARGET_UID="$target_uid"
export TARGET_GID="$target_gid"
export TESTFILE="tmpfile_replay.$$"

for sync_mode in standard always; do
        log_note "Running O_TMPFILE crash-replay test with sync=$sync_mode"
        log_must zfs set sync="$sync_mode" "$TESTPOOL/$TESTFS"
        log_must "$STF_SUITE/tests/functional/tmpfile/tmpfile_replay"
done

log_pass "Crash-replay of O_TMPFILE linking (xattr=sa) verified."
