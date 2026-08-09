#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/history/history_common.kshlib

#
# DESCRIPTION:
#	Pool history records all recursive operations.
#
# STRATEGY:
#	1. Create a filesystem and several sub-filesystems in it.
#	2. Make a recursive snapshot.
#	3. Verify pool history records all the recursive operations.
#	4. Do the same verification for hold, release, inherit, rollback and
#	   destroy.
#

verify_runnable "global"

function cleanup
{
	datasetexists $root_testfs && destroy_dataset $root_testfs -rf
	log_must zfs create $root_testfs
}

log_assert "Pool history records all recursive operations."
log_onexit cleanup

root_testfs=$TESTPOOL/$TESTFS
for fs in $root_testfs/fs{1..3}; do
	log_must zfs create $fs
done

run_and_verify "zfs snapshot -r $root_testfs@snap" "-i"
run_and_verify "zfs hold -r tag $root_testfs@snap" "-i"
run_and_verify "zfs release -r tag $root_testfs@snap" "-i"
log_must zfs snapshot $root_testfs@snap2
log_must zfs snapshot $root_testfs@snap3
run_and_verify "zfs rollback -r $root_testfs@snap" "-i"
run_and_verify "zfs inherit -r mountpoint $root_testfs" "-i"
run_and_verify "zfs destroy -r $root_testfs" "-i"

log_pass "Pool history records all recursive operations."
