#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2026, Gluesys. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#	Verify 'zfs userspace' reports correct quota values whether the
#	dataset is mounted or not, and does not fail with EBUSY when
#	raced against 'zfs mount'.
#
# STRATEGY:
#	1. Create a dataset with a userquota and write 50M of data.
#	2. Check quota and used values while mounted.
#	3. Unmount and verify the same values are still readable.
#	4. Remount and re-verify values are consistent.
#	5. Race 'zfs userspace' against 'zfs mount' 50 times; expect no EBUSY.
#

function cleanup
{
	zfs unmount $RACE_FS 2>/dev/null
	datasetexists $RACE_FS && destroy_dataset $RACE_FS
	rm -f "$ERRFILE" "$DATAFILE"
}

log_onexit cleanup

typeset RACE_FS=$QFS/userspace_race
typeset ERRFILE=/tmp/userspace_005_err.$$
typeset DATAFILE
typeset -i ITERS=50

log_assert "zfs userspace shows correct quota values and does not EBUSY on mount race"

log_must zfs create $RACE_FS
log_must zfs set userquota@$QUSER1=100m $RACE_FS
mkmount_writable $RACE_FS
DATAFILE=$(get_prop mountpoint $RACE_FS)/userspace_005_data.$$
log_must user_run $QUSER1 mkfile 50m $DATAFILE
sync_all_pools

log_note "check userspace values while mounted"
log_must eval "zfs userspace $RACE_FS | grep $QUSER1 | grep 100M"
log_must eval "zfs userspace $RACE_FS | grep $QUSER1 | grep '50\\..*M'"

log_must zfs unmount $RACE_FS
log_note "check userspace values while unmounted"
log_must eval "zfs userspace $RACE_FS | grep $QUSER1 | grep 100M"
log_must eval "zfs userspace $RACE_FS | grep $QUSER1 | grep '50\\..*M'"

log_must zfs mount $RACE_FS
log_note "check userspace values after remount"
log_must eval "zfs userspace $RACE_FS | grep $QUSER1 | grep 100M"
log_must eval "zfs userspace $RACE_FS | grep $QUSER1 | grep '50\\..*M'"

typeset -i i=0 ebusy=0
while (( i < ITERS )); do
	log_must zfs unmount $RACE_FS
	zfs mount $RACE_FS &
	typeset mpid=$!
	zfs userspace $RACE_FS >"$ERRFILE" 2>&1
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

log_pass "zfs userspace shows correct quota values and does not EBUSY on mount race"
