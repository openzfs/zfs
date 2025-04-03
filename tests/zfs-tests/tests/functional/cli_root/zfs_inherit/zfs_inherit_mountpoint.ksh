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
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zfs inherit should inherit mountpoint on mountpoint=none children
#
# STRATEGY:
#	1. Create a set of nested datasets with mountpoint=none
#	2. Verify datasets aren't mounted
#	3. Inherit mountpoint and verify all datasets are now mounted
#

verify_runnable "both"

function inherit_cleanup
{
	log_must zfs destroy -fR $TESTPOOL/inherit_test
}

log_onexit inherit_cleanup


log_must zfs create -o mountpoint=none $TESTPOOL/inherit_test
log_must zfs create $TESTPOOL/inherit_test/child

if ismounted $TESTPOOL/inherit_test; then
	log_fail "$TESTPOOL/inherit_test is mounted"
fi
if ismounted $TESTPOOL/inherit_test/child; then
	log_fail "$TESTPOOL/inherit_test/child is mounted"
fi

log_must zfs inherit mountpoint $TESTPOOL/inherit_test

if ! ismounted $TESTPOOL/inherit_test; then
	log_fail "$TESTPOOL/inherit_test is not mounted"
fi
if ! ismounted $TESTPOOL/inherit_test/child; then
	log_fail "$TESTPOOL/inherit_test/child is not mounted"
fi

log_pass "Verified mountpoint for mountpoint=none children inherited."
