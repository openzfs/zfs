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

export LC_ALL="C"
export LANG="C"

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#	Verify error handling and unsupported operations with quota(1) and
#	setquota(8) via quotactl_fd(2).
#
# STRATEGY:
#	1. Verify setting individual grace times (-T, QIF_BTIME | QIF_ITIME)
#	   fails with EINVAL.
#	2. Verify setting global grace periods (-t, Q_SETINFO) fails with
#	   ENOSYS.
#	3. Verify setting quota on a read-only filesystem fails with EROFS.
#	4. Verify setting quota on a snapshot fails.
#	5. Verify invalid quota options and non-existent mountpoints fail.
#	6. Verify an unprivileged user cannot read or set another user's
#	   quota (EPERM).
#

verify_runnable "both"

function cleanup
{
	zfs destroy $SNAP >/dev/null 2>&1
	zfs set readonly=off $QFS >/dev/null 2>&1
	cleanup_quota
}

log_onexit cleanup

log_assert "quota(1)/setquota(8) or quotactl_fd reject unsupported operations and read-only targets"

mkmount_writable $QFS
setup_quota_tools

SNAP=$QFS@snap_neg

out=$(do_setquota "$TESTDIR" grace $QUSER1 2>&1)
if [[ $? -eq 0 ]] || ! echo "$out" | grep -qi "Invalid argument"; then
	log_fail "Expected Invalid argument for QIF_BTIME|QIF_ITIME, got: $out"
fi

out=$(do_setquota "$TESTDIR" graceinfo 2>&1)
if [[ $? -eq 0 ]] || ! echo "$out" | grep -qi "Function not implemented"; then
	log_fail "Expected Function not implemented for Q_SETINFO, got: $out"
fi

log_must zfs set readonly=on $QFS
out=$(do_setquota "$TESTDIR" usr $QUSER1 102400 0 2>&1)
if [[ $? -eq 0 ]] || ! echo "$out" | grep -qi "Read-only file system"; then
	log_fail "Expected Read-only file system for readonly mount, got: $out"
fi
log_must zfs set readonly=off $QFS

log_must zfs snapshot $SNAP
SNAPDIR=$TESTDIR/.zfs/snapshot/snap_neg
if [[ -d "$SNAPDIR/." ]]; then
	log_mustnot do_setquota "$SNAPDIR/." usr $QUSER1 102400 0
fi
log_must zfs destroy $SNAP

log_mustnot do_setquota "$TESTDIR" invalid $QUSER1 102400 0
log_mustnot do_quota "$TESTDIR/nonexistent_mount" usr $QUSER1

out=$(do_quota -U $QUSER1 "$TESTDIR" usr $QUSER2 2>&1)
if [[ $? -eq 0 ]] || ! echo "$out" | \
    grep -qiE "Operation not permitted|Permission denied"; then
	log_fail "Expected permission error reading other user's" \
	    "quota, got: $out"
fi

out=$(do_setquota -U $QUSER1 "$TESTDIR" usr $QUSER2 102400 0 2>&1)
if [[ $? -eq 0 ]] || ! echo "$out" | \
    grep -qiE "Operation not permitted|Permission denied"; then
	log_fail "Expected permission error setting other user's" \
	    "quota, got: $out"
fi

log_pass "Invalid quota operations and read-only targets were rejected"
