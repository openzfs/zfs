#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
#	Ensure that the MMP thread is writing uberblocks.
#
# STRATEGY:
#	1. Set zfs_txg_timeout to large value
#	2. Create a zpool
#	3. Find the current "best" uberblock
#	4. Sleep for enough time for a potential uberblock update
#	5. Find the current "best" uberblock
#	6. If the uberblock never changed, fail
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
	log_must set_tunable64 TXG_TIMEOUT $TXG_TIMEOUT_DEFAULT
	log_must rm -f $PREV_UBER $CURR_UBER
	log_must mmp_clear_hostid
}

log_assert "mmp thread writes uberblocks (MMP)"
log_onexit cleanup

log_must set_tunable64 TXG_TIMEOUT $TXG_TIMEOUT_LONG
log_must mmp_set_hostid $HOSTID1

default_setup_noexit $DISK
log_must zpool set multihost=on $TESTPOOL

log_must eval "zdb -u $TESTPOOL > $PREV_UBER"
log_must sleep 5
log_must eval "zdb -u $TESTPOOL > $CURR_UBER"

if diff -u "$CURR_UBER" "$PREV_UBER"; then
	log_fail "mmp failed to update uberblocks"
fi

log_pass "mmp thread writes uberblocks (MMP) passed"
