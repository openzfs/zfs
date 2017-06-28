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
#	Force importing an active pool when the hostid in the pool is
#	equivalent to the current host's hostid, even though dangerous,
#	it should succeed.
#
# STRATEGY:
#	1. Run ztest in the background with hostid x.
#	2. Set hostid to x.
#	3. A `zpool import` on the pool created by ztest should succeed.
#
# NOTES:
#	This test cases tests a situation which we do not support. Two hosts
#	with the same hostid is strictly unsupported. While it is expected
#	that a zpool import will succeed in this circumstance, a kernel panic
#	and other known issues may result. This test case may be useful in
#	the future in situations when pools can be imported in both kernel
#	and user space on the same node.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg

verify_runnable "both"
ZTESTPID=

function cleanup
{
	if [ -n "$ZTESTPID" ]; then
		if ps -p $ZTESTPID > /dev/null; then
			log_must kill -s 9 $ZTESTPID
			wait $ZTESTPID
		fi
	fi

	if poolexists ztest; then
                log_must zpool destroy ztest
        fi

	log_must rm -rf "$TEST_BASE_DIR/mmp_vdevs"
	set_spl_tunable spl_hostid $SPL_HOSTID_DEFAULT
}

log_assert "zpool import -f succeeds on active pool with same hostid (MMP)"
log_onexit cleanup

log_must mkdir "$TEST_BASE_DIR/mmp_vdevs"

log_note "Starting ztest in the background"
export ZFS_HOSTID="$SPL_HOSTID1"
log_must eval "ztest -t1 -k0 -f $TEST_BASE_DIR/mmp_vdevs > /dev/null 2>&1 &"
ZTESTPID=$!
if ! ps -p $ZTESTPID > /dev/null; then
	log_fail "ztest failed to start"
fi

if ! set_spl_tunable spl_hostid "$ZFS_HOSTID"; then
	log_fail "Failed to set spl_hostid to $ZFS_HOSTID"
fi
log_must zpool import -f -d "$TEST_BASE_DIR/mmp_vdevs" ztest

log_pass "zpool import -f succeeds on active pool with same hostid (MMP) passed"
