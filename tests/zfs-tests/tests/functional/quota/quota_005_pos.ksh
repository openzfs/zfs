#! /bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/quota/quota.kshlib

#
# DESCRIPTION:
#
# Verify that quota doesn't inherit its value from parent.
#
# STRATEGY:
# 1) Set quota for parents
# 2) Create a filesystem tree
# 3) Verify that the 'quota' for descendent doesnot inherit the value.
#
###############################################################################

verify_runnable "both"

function cleanup
{
	datasetexists $fs_child && destroy_dataset $fs_child

	reset_quota $fs
}

log_onexit cleanup

log_assert "Verify that quota does not inherit its value from parent."

fs=$TESTPOOL/$TESTFS
fs_child=$TESTPOOL/$TESTFS/$TESTFS

space_avail=$(get_prop available $fs)
quota_val=$(get_prop quota $fs)
typeset -li quotasize=$space_avail
((quotasize = quotasize * 2 ))
log_must zfs set quota=$quotasize $fs

log_must zfs create $fs_child
quota_space=$(get_prop quota $fs_child)
[[ $quota_space == $quotasize ]] && \
	log_fail "The quota of child dataset inherits its value from parent."

log_pass "quota does not inherit its value from parent as expected."
