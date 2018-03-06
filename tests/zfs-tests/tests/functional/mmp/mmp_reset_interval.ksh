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
#	Ensure that the MMP thread is notified when zfs_multihost_interval is
#	reduced.
#
# STRATEGY:
#	1. Set zfs_multihost_interval to much longer than the test duration
#	2. Create a zpool and enable multihost
#	3. Verify no MMP writes occurred
#	4. Set zfs_multihost_interval to 1 second
#	5. Sleep briefly
#	6. Verify MMP writes began
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
	log_must set_tunable64 zfs_multihost_interval $MMP_INTERVAL_DEFAULT
	log_must mmp_clear_hostid
}

log_assert "mmp threads notified when zfs_multihost_interval reduced"
log_onexit cleanup

log_must set_tunable64 zfs_multihost_interval $MMP_INTERVAL_HOUR
log_must mmp_set_hostid $HOSTID1

default_setup_noexit $DISK
log_must zpool set multihost=on $TESTPOOL

clear_mmp_history
log_must set_tunable64 zfs_multihost_interval $MMP_INTERVAL_DEFAULT
uber_count=$(count_mmp_writes $TESTPOOL 1)

if [ $uber_count -eq 0 ]; then
	log_fail "mmp writes did not start when zfs_multihost_interval reduced"
fi

log_pass "mmp threads notified when zfs_multihost_interval reduced"
