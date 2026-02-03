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
# Copyright (c) 2026 by Klara Inc.
#

#
# Description:
# Verify that metaslab weight algorithm selection works correctly.
#
# Strategy:
# 1. Set the zfs_active_weightfunc to auto.
# 2. Create a pool.
# 3. Repeat steps 1 and 2 with the other valid values.
#

. $STF_SUITE/include/libtest.shlib

log_assert "Metaslab weight algorithm selection works correctly."

function cleanup
{
	restore_tunable_string ACTIVE_WEIGHTFUNC
	zpool destroy $TESTPOOL
}
log_onexit cleanup

save_tunable ACTIVE_WEIGHTFUNC

for value in "auto" "space" "space_v2" "segment"
do
	set_tunable_string ACTIVE_WEIGHTFUNC
	log_must zpool create -f $TESTPOOL $DISKS
	log_must zpool destroy $TESTPOOL
done

log_pass "Metaslab weight algorithm selection works correctly."
