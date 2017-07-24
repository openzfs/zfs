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
#	Verify MMP behaves correctly when failing to write uberblocks.
#
# STRATEGY:
#	1. Create a mirrored pool and enable multihost
#	2. Inject a 50% failure rate when writing uberblocks to a device
#	3. Delay briefly for additional MMP writes to complete
#	4. Verify the failed uberblock writes did not prevent MMP updates
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	zinject -c all
	default_cleanup_noexit
	log_must mmp_clear_hostid
}

log_assert "mmp behaves correctly when failing to write uberblocks."
log_onexit cleanup

log_must mmp_set_hostid $HOSTID1
default_mirror_setup_noexit $DISKS
log_must zpool set multihost=on $TESTPOOL
log_must zinject -d ${DISK[0]} -e io -T write -f 50 $TESTPOOL -L uber

prev_count=$(wc -l /proc/spl/kstat/zfs/$TESTPOOL/multihost | cut -f1 -d' ')
log_must sleep 3
curr_count=$(wc -l /proc/spl/kstat/zfs/$TESTPOOL/multihost | cut -f1 -d' ')

if [ $curr_count -eq $prev_count ]; then
	log_fail "mmp writes did not occur when uberblock IO errors injected"
fi

log_pass "mmp correctly wrote uberblocks when IO errors injected"
