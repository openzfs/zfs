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
#	zfs_multihost_history should report both writes issued and gaps
#
# STRATEGY:
#	1. Create a 2-vdev pool with mmp enabled
#	2. Delay writes by 2*MMP_INTERVAL_DEFAULT
#	3. Check multihost_history for both issued writes, and for gaps where
#	no write could be issued because all vdevs are busy
#
# During the first MMP_INTERVAL period 2 MMP writes will be issued - one to
# each vdev.  At the third scheduled attempt to write, at time t0+MMP_INTERVAL,
# both vdevs will still have outstanding writes, so a skipped write entry will
# be recorded in the multihost_history.


. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	mmp_pool_destroy $MMP_POOL $MMP_DIR
	log_must mmp_clear_hostid
}

log_assert "zfs_multihost_history records writes and skipped writes"
log_onexit cleanup

mmp_pool_create_simple $MMP_POOL $MMP_DIR
log_must zinject -d $MMP_DIR/vdev1 -D$((2*MMP_INTERVAL_DEFAULT)):10 $MMP_POOL
log_must zinject -d $MMP_DIR/vdev2 -D$((2*MMP_INTERVAL_DEFAULT)):10 $MMP_POOL

mmp_writes=$(count_mmp_writes $MMP_POOL $((MMP_INTERVAL_DEFAULT/1000)))
mmp_skips=$(count_skipped_mmp_writes $MMP_POOL $((MMP_INTERVAL_DEFAULT/1000)))

if [ $mmp_writes -lt 1 ]; then
	log_fail "mmp writes entries missing when delays injected"
fi

if [ $mmp_skips -lt 1 ]; then
	log_fail "mmp skipped write entries missing when delays injected"
fi

log_pass "zfs_multihost_history records writes and skipped writes"
