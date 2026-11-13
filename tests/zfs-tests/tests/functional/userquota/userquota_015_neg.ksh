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
NUM_UID=$(quota_id_to_num usr $QUSER1)

#
# 1. Setting individual user grace times (QIF_BTIME | QIF_ITIME = 48
#    passed to Q_SETQUOTA) must be rejected with EINVAL.
#
if [[ "$QUOTA_MODE" == "quota_tools" ]]; then
	out=$("$SETQUOTA_CMD" -u $QUSER1 -T unset unset $TESTDIR 2>&1)
else
	out=$(quotactl_fd setmask $TESTDIR usr $NUM_UID 48 2>&1)
fi
if [[ $? -eq 0 ]] || ! echo "$out" | grep -qi "Invalid argument"; then
	log_fail "Expected Invalid argument for QIF_BTIME|QIF_ITIME, got: $out"
fi

#
# 2. Setting global grace period (Q_SETINFO, which ZFS does not
#    implement) must fail with ENOSYS ("Function not implemented").
#
if [[ "$QUOTA_MODE" == "quota_tools" ]]; then
	out=$("$SETQUOTA_CMD" -u -t 86400 86400 $TESTDIR 2>&1)
else
	out=$(quotactl_fd setinfo $TESTDIR usr 2>&1)
fi
if [[ $? -eq 0 ]] || ! echo "$out" | grep -qi "Function not implemented"; then
	log_fail "Expected Function not implemented for Q_SETINFO, got: $out"
fi

#
# 3. Verify setting quota on a read-only filesystem fails with EROFS.
#
log_must zfs set readonly=on $QFS
if [[ "$QUOTA_MODE" == "quota_tools" ]]; then
	out=$("$SETQUOTA_CMD" -u $QUSER1 102400 102400 0 0 $TESTDIR 2>&1)
else
	out=$(quotactl_fd set $TESTDIR usr $NUM_UID 104857600 0 2>&1)
fi
if [[ $? -eq 0 ]] || ! echo "$out" | grep -qi "Read-only file system"; then
	log_fail "Expected Read-only file system for readonly mount, got: $out"
fi
log_must zfs set readonly=off $QFS

#
# 4. Verify setting quota on a snapshot fails.
#
log_must zfs snapshot $SNAP
SNAPDIR=$TESTDIR/.zfs/snapshot/snap_neg
if [[ -d "$SNAPDIR/." ]]; then
	if [[ "$QUOTA_MODE" == "quota_tools" ]]; then
		log_mustnot "$SETQUOTA_CMD" -u $QUSER1 102400 102400 0 0 \
		    "$SNAPDIR/."
	else
		log_mustnot quotactl_fd set "$SNAPDIR/." usr $NUM_UID \
		    104857600 0
	fi
fi
log_must zfs destroy $SNAP

#
# 5. Verify invalid arguments and non-mountpoint paths fail.
#
if [[ "$QUOTA_MODE" == "quota_tools" ]]; then
	log_mustnot "$SETQUOTA_CMD" -u -g $QUSER1 102400 102400 0 0 $TESTDIR
	log_mustnot "$QUOTA_CMD" --filesystem="$TESTDIR/nonexistent_mount" \
	    -u $QUSER1
else
	log_mustnot quotactl_fd set $TESTDIR invalid $NUM_UID 104857600
	log_mustnot quotactl_fd get "$TESTDIR/nonexistent_mount" usr 999999
fi

log_pass "Invalid quota operations and read-only targets were rejected"
