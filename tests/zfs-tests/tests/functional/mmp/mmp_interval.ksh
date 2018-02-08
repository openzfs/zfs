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
#	zfs_multihost_interval should only accept valid values.
#
# STRATEGY:
#	1. Set zfs_multihost_interval to invalid values (negative).
#	2. Set zfs_multihost_interval to valid values.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	log_must set_tunable64 zfs_multihost_interval $MMP_INTERVAL_DEFAULT
}

log_assert "zfs_multihost_interval cannot be set to an invalid value"
log_onexit cleanup

log_mustnot set_tunable64 zfs_multihost_interval -1
log_must set_tunable64 zfs_multihost_interval $MMP_INTERVAL_MIN
log_must set_tunable64 zfs_multihost_interval $MMP_INTERVAL_DEFAULT

log_pass "zfs_multihost_interval cannot be set to an invalid value"
