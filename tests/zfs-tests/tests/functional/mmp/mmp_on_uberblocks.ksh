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
# Copyright (c) 2026 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Ensure that MMP updates uberblocks with MMP info at expected intervals. 
#
# STRATEGY:
#	1. Create a zpool
#	2. Clear multihost history
#	3. Sleep for 10s, then collect count of uberblocks written
#	4. Verify the mmp updates are within 20% of the expected target
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

DURATION=10
NDISKS=$(echo $DISKS | wc -w)
MMP_INTERVAL=$(get_tunable MULTIHOST_INTERVAL)
TARGET=$((($NDISKS * $DURATION * 1000) / $MMP_INTERVAL))

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must mmp_clear_hostid
}

log_assert "Ensure MMP uberblocks update at the correct interval"
log_onexit cleanup

log_must mmp_set_hostid $HOSTID1

log_must zpool create -f $TESTPOOL $DISKS
log_must zpool set multihost=on $TESTPOOL

clear_mmp_history
MMP_WRITES=$(count_mmp_writes $TESTPOOL $DURATION)

log_note "Uberblock changed $MMP_WRITES times"
log_must within_percent $MMP_WRITES $TARGET 80

log_pass "Ensure MMP uberblocks update at the correct interval passed"
