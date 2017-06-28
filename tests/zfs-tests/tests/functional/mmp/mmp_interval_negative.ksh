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
#	zfs_mmp_interval should never be able to be negative
#
# STRATEGY:
#	1. Set zfs_mmp_interval to negative value, should fail
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg

verify_runnable "both"

function cleanup
{
	set_tunable64 zfs_mmp_interval $MMP_INTERVAL_DEFAULT
}

log_assert "zfs_mmp_interval cannot be set to a negative value"
log_onexit cleanup

if set_tunable64 zfs_mmp_interval -1; then
	log_fail "zfs_mmp_interval was set to a negative value"
fi

log_pass "zfs_mmp_interval cannot be set to a negative value passed"
