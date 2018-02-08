#!/bin/ksh -p
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
# Copyright (c) 2016, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION: Check various invalid snapshot error cases
#

verify_runnable "global"

fs1=$TESTPOOL/$TESTFS/testchild1
fs2=$TESTPOOL/$TESTFS/testchild2

function cleanup
{
	for fs in $fs1 $fs2; do
		datasetexists $fs && log_must zfs destroy -R $fs
	done
}

log_onexit cleanup

log_must zfs create $fs1
log_must zfs create $fs2
log_must zfs snapshot $fs1@snap1

log_must_program $TESTPOOL $ZCP_ROOT/synctask_core/tst.snapshot_neg.zcp $fs1 $fs2

log_pass "zfs.sync.snapshot returns correct errors on invalid input"

