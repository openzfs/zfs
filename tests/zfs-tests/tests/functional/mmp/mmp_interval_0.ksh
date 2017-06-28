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
#	When zfs_mmp_interval is set to 0, ensure that leaf vdev
#	uberblocks are not updated.
#
# STRATEGY:
#	1. Set zfs_mmp_interval to 0 (disables mmp)
#	2. Set zfs_txg_timeout to large value
#	3. Create a zpool
#	4. Force a sync on the zpool
#	5. Find the current "best" uberblock
#	6. Sleep for enough time for uberblocks to change
#	7. Find the current "best" uberblock
#	8. If the uberblock changed, fail
#	9. Set zfs_mmp_interval to 100
#	10. Sleep for enough time for uberblocks to change
#	11. Find the current "best" uberblock
#	12. If uberblocks didn't change, fail
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
	set_tunable64 zfs_mmp_interval $MMP_INTERVAL_DEFAULT
	set_tunable64 zfs_txg_timeout $TXG_TIMEOUT_DEFAULT
	log_must rm -f $PREV_UBER $CURR_UBER
}

log_assert "mmp thread won't write uberblocks with zfs_mmp_interval=0"
log_onexit cleanup

if ! set_tunable64 zfs_mmp_interval $MMP_INTERVAL_OFF; then
	log_fail "Failed to set zfs_mmp_interval to $MMP_INTERVAL_OFF"
fi

if ! set_tunable64 zfs_txg_timeout $TXG_TIMEOUT_LONG; then
	log_fail "Failed to set zfs_txg_timeout to $TXG_TIMEOUT_LONG"
fi

default_setup_noexit $DISK
sync_pool $TESTPOOL
log_must zdb -u $TESTPOOL > $PREV_UBER
log_must sleep 5
log_must zdb -u $TESTPOOL > $CURR_UBER

if ! diff "$CURR_UBER" "$PREV_UBER"; then
	log_fail "mmp thread has updated an uberblock"
fi

if ! set_tunable64 zfs_mmp_interval $MMP_INTERVAL_SHORT; then
	log_fail "Failed to set zfs_mmp_interval to $MMP_INTERVAL_SHORT"
fi

log_must sleep 3
log_must zdb -u $TESTPOOL > $CURR_UBER
if diff "$CURR_UBER" "$PREV_UBER"; then
	log_fail "mmp failed to update uberblocks"
fi

log_pass "mmp thread won't write uberblocks with zfs_mmp_interval=0 passed"
