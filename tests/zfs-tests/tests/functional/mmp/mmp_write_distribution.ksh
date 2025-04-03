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
#	Verify MMP writes are distributed evenly among leaves
#
# STRATEGY:
#	1. Create an asymmetric mirrored pool
#	2. Enable multihost and multihost_history
#	3. Delay for MMP writes to occur
#	4. Verify the MMP writes are distributed evenly across leaf vdevs
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	log_must zpool destroy $MMP_POOL
	log_must rm $MMP_DIR/file.{0..7}
	log_must rm $MMP_HISTORY_TMP
	log_must rmdir $MMP_DIR
	log_must mmp_clear_hostid
}

log_assert "mmp writes are evenly distributed across leaf vdevs"
log_onexit cleanup

MMP_HISTORY_TMP=$MMP_DIR/history

# Step 1
log_must mkdir -p $MMP_DIR
log_must truncate -s 128M $MMP_DIR/file.{0..7}
log_must zpool create -f $MMP_POOL mirror $MMP_DIR/file.{0..1} mirror $MMP_DIR/file.{2..7}

# Step 2
log_must mmp_set_hostid $HOSTID1
log_must zpool set multihost=on $MMP_POOL
set_tunable64 MULTIHOST_HISTORY 0
set_tunable64 MULTIHOST_HISTORY 40

# Step 3
# default settings, every leaf written once/second
sleep 4

# Step 4
typeset -i min_writes=999
typeset -i max_writes=0
typeset -i write_count
# copy to get as close to a consistent view as possible
kstat_pool $MMP_POOL multihost > $MMP_HISTORY_TMP
for x in {0..7}; do
	write_count=$(grep -c file.${x} $MMP_HISTORY_TMP)
	if [ $write_count -lt $min_writes ]; then
		min_writes=$write_count
	fi
	if [ $write_count -gt $max_writes ]; then
		max_writes=$write_count
	fi
done
log_note "mmp min_writes $min_writes max_writes $max_writes"

if [ $min_writes -lt 1 ]; then
	log_fail "mmp writes were not counted correctly"
fi

if [ $((max_writes - min_writes)) -gt 1 ]; then
	log_fail "mmp writes were not evenly distributed across leaf vdevs"
fi

log_pass "mmp writes were evenly distributed across leaf vdevs"
