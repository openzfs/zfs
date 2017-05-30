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
#	When zfs_mmp_interval is set to 0, there should be no activity
#	checks when importing a pool on a different host.
#
# STRATEGY:
#	1. Set zfs_mmp_interval to 0 (disables mmp)
#	2. Set hostid
#	3. Create a zpool
#	4. zpool export -F for ONLINE VDEV
#	5. Change hostid
#	6. Attempt a zpool import -f
#	7. Fail if zpool import -f took more than 2 seconds
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
	set_spl_tunable spl_hostid $SPL_HOSTID_DEFAULT
	set_tunable64 zfs_mmp_interval $MMP_INTERVAL_DEFAULT
}

log_assert "zfs_mmp_interval=0 should skip activity checks"
log_onexit cleanup

if ! set_spl_tunable spl_hostid $SPL_HOSTID1; then
	log_fail "Failed to set spl_hostid to $SPL_HOSTID1"
fi

if ! set_tunable64 zfs_mmp_interval $MMP_INTERVAL_OFF; then
	log_fail "Failed to set zfs_mmp_interval to $MMP_INTERVAL_OFF"
fi

default_setup_noexit $DISK
log_must zpool export -F $TESTPOOL

if ! set_spl_tunable spl_hostid $SPL_HOSTID2; then
	log_fail "Failed to set spl_hostid to $SPL_HOSTID2"
fi
SECONDS=0
log_must zpool import -f $TESTPOOL

if [[ $SECONDS -gt $ZPOOL_IMPORT_DURATION ]]; then
	log_fail "mmp activity check occured, expected no activity check"
fi

log_pass "zfs_mmp_interval=0 should skip activity checks passed"
