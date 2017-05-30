#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Ensure that MMP updates uberblocks at the expected intervals.
#
# STRATEGY:
#	1. Set zfs_txg_timeout to large value
#	2. Create a zpool
#	3. Force a sync on the zpool
#	4. Find the current "best" uberblock
#	5. Loop for 10 seconds, increment counter for each change in UB
#	6. If number of changes seen is less than min threshold, then fail
#	7. If number of changes seen is more than max threshold, then fail
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg

verify_runnable "both"

UBER_CHANGES=0

function cleanup
{
	default_cleanup_noexit
	set_tunable64 zfs_txg_timeout $TXG_TIMEOUT_DEFAULT
	log_must rm -f $PREV_UBER $CURR_UBER
}

log_assert "Ensure MMP uberblocks update at the correct interval"
log_onexit cleanup

if ! set_tunable64 zfs_txg_timeout $TXG_TIMEOUT_LONG ; then
	log_fail "Failed to set zfs_txg_timeout"
fi

default_setup_noexit $DISK
sync_pool $TESTPOOL

log_must zdb -u $TESTPOOL > $PREV_UBER
SECONDS=0
while [[ $SECONDS -le 10 ]]; do
	log_must zdb -u $TESTPOOL > $CURR_UBER
	if ! diff "$CURR_UBER" "$PREV_UBER"; then
		(( UBER_CHANGES = UBER_CHANGES + 1 ))
		log_must mv "$CURR_UBER" "$PREV_UBER"
	fi
done

log_note "Uberblock changed $UBER_CHANGES times"

if [[ $UBER_CHANGES -lt 8 ]]; then
	log_fail "Fewer uberblock writes occured than expected (10)"
fi

if [[ $UBER_CHANGES -gt 12 ]]; then
	log_fail "More uberblock writes occured than expected (10)"
fi

log_pass "Ensure MMP uberblocks update at the correct interval passed"
