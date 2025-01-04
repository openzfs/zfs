#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

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

#
# Copyright (c) 2018 Lawrence Livermore National Security, LLC.
# Copyright (c) 2018 by Nutanix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

#
# Description:
# zdb will work while multihost is enabled.
#
# Strategy:
# 1. Create a pool
# 2. Enable multihost
# 3. Run zdb -d with pool and dataset arguments.
# 4. Create a checkpoint
# 5. Run zdb -kd with pool and dataset arguments.
# 6. Discard the checkpoint
# 7. Export the pool
# 8. Run zdb -ed with pool and dataset arguments.
#

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	for DISK in $DISKS; do
		zpool labelclear -f $DEV_RDSKDIR/$DISK
	done
	log_must mmp_clear_hostid
}

log_assert "Verify zdb -d works while multihost is enabled"
log_onexit cleanup

verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS
log_must mmp_set_hostid $HOSTID1
log_must zpool set multihost=on $TESTPOOL
log_must zfs snap $TESTPOOL/$TESTFS@snap

log_must zdb -d $TESTPOOL
log_must zdb -d $TESTPOOL/
log_must zdb -d $TESTPOOL/$TESTFS
log_must zdb -d $TESTPOOL/$TESTFS@snap

log_must zpool checkpoint $TESTPOOL
log_must zdb -kd $TESTPOOL
log_must zdb -kd $TESTPOOL/
log_must zdb -kd $TESTPOOL/$TESTFS
log_must zdb -kd $TESTPOOL/$TESTFS@snap
log_must zpool checkpoint -d $TESTPOOL

log_must zpool export $TESTPOOL

log_must zdb -ed $TESTPOOL
log_must zdb -ed $TESTPOOL/
log_must zdb -ed $TESTPOOL/$TESTFS
log_must zdb -ed $TESTPOOL/$TESTFS@snap

log_must zpool import $TESTPOOL

cleanup

log_pass "zdb -d works while multihost is enabled"
