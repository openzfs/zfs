#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
	datasetexists $fs_child && \
		log_must zfs destroy $fs_child

	log_must zfs set quota=$quota_val $fs
}

log_onexit cleanup

log_assert "Verify that quota doesnot inherit its value from parent."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
fs_child=$TESTPOOL/$TESTFS/$TESTFS

space_avail=$(get_prop available $fs)
quota_val=$(get_prop quota $fs)
typeset -i quotasize=$space_avail
((quotasize = quotasize * 2 ))
log_must zfs set quota=$quotasize $fs

log_must zfs create $fs_child
quota_space=$(get_prop quota $fs_child)
[[ $quota_space == $quotasize ]] && \
	log_fail "The quota of child dataset inherits its value from parent."

log_pass "quota doesnot inherit its value from parent as expected."
