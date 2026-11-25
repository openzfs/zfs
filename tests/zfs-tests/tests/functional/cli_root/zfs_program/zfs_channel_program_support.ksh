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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 	Verify zfs channel program support
#
# STRATEGY:
#	1. Verify whether zfs channel program support
#	   has been enabled or disabled
#

verify_runnable "both"

function cleanup
{
	return 0
}
log_onexit cleanup

# 1. Verify zfs channel program support is disabled
if zcp_support $TESTPOOL; then
	log_must zfs program $TESTPOOL - <<<"zfs.exists(\"$TESTPOOL\")" 2>&1
	log_pass "Channel programs are enabled"
else
	log_mustnot zfs program $TESTPOOL - <<<"zfs.exists(\"$TESTPOOL\")" 2>&1
	log_pass "Channel programs are disabled"
fi
