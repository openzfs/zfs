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
# Copyright (c) 2026, Gluesys. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#	Verify 'zfs groupspace' reports correct quota values whether the
#	dataset is mounted or not, and does not fail with EBUSY when
#	raced against 'zfs mount'.
#
# STRATEGY:
#	1. Create a dataset with a groupquota and write 50M of data.
#	2. Check quota and used values while mounted.
#	3. Unmount and verify the same values are still readable.
#	4. Remount and re-verify values are consistent.
#	5. Race 'zfs groupspace' against 'zfs mount' 50 times; expect no EBUSY.
#

function cleanup
{
	zfs unmount $RACE_FS 2>/dev/null
	datasetexists $RACE_FS && destroy_dataset $RACE_FS
	rm -f "$ERRFILE" "$DATAFILE"
}

log_onexit cleanup

typeset RACE_FS=$QFS/groupspace_race
typeset ERRFILE=/tmp/groupspace_005_err.$$
typeset DATAFILE
typeset -i ITERS=50

log_assert "zfs groupspace shows correct quota values and does not EBUSY on mount race"

log_must zfs create $RACE_FS
log_must zfs set groupquota@$QGROUP=100m $RACE_FS
mkmount_writable $RACE_FS
DATAFILE=$(get_prop mountpoint $RACE_FS)/groupspace_005_data.$$
log_must user_run $QUSER1 mkfile 50m $DATAFILE
sync_all_pools

log_note "check groupspace values while mounted"
log_must eval "zfs groupspace $RACE_FS | grep $QGROUP | grep 100M"
log_must eval "zfs groupspace $RACE_FS | grep $QGROUP | grep '50\\..*M'"

log_must zfs unmount $RACE_FS
log_note "check groupspace values while unmounted"
log_must eval "zfs groupspace $RACE_FS | grep $QGROUP | grep 100M"
log_must eval "zfs groupspace $RACE_FS | grep $QGROUP | grep '50\\..*M'"

log_must zfs mount $RACE_FS
log_note "check groupspace values after remount"
log_must eval "zfs groupspace $RACE_FS | grep $QGROUP | grep 100M"
log_must eval "zfs groupspace $RACE_FS | grep $QGROUP | grep '50\\..*M'"

typeset -i i=0 ebusy=0
while (( i < ITERS )); do
	log_must zfs unmount $RACE_FS
	zfs mount $RACE_FS &
	typeset mpid=$!
	zfs groupspace $RACE_FS >"$ERRFILE" 2>&1
	typeset rc=$?
	wait $mpid
	if (( rc != 0 )) && grep -qi "busy" "$ERRFILE" 2>/dev/null; then
		(( ebusy++ ))
		log_note "Iteration $i: EBUSY: $(cat $ERRFILE)"
	fi
	rm -f "$ERRFILE"
	(( i++ ))
done

(( ebusy > 0 )) && log_fail "EBUSY seen $ebusy/$ITERS times"

log_pass "zfs groupspace shows correct quota values and does not EBUSY on mount race"
