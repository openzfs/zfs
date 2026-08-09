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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the basic function user|group used
#
#
# STRATEGY:
#       1. Write some data to fs by normal user and check the user|group used
#

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "Check the basic function of {user|group} used"

sync_pool
typeset user_used=$(get_prop "userused@$QUSER1" $QFS)
typeset group_used=$(get_prop "groupused@$QGROUP" $QFS)
typeset file_size='100m'

if [[ $user_used != 0 ]]; then
	log_fail "FAIL: userused is $user_used, should be 0"
fi
if [[ $group_used != 0 ]]; then
	log_fail "FAIL: groupused is $group_used, should be 0"
fi

mkmount_writable $QFS
log_must user_run $QUSER1 mkfile $file_size $QFILE
sync_pool

user_used=$(get_prop "userused@$QUSER1" $QFS)
group_used=$(get_prop "groupused@$QGROUP" $QFS)

# get_prop() reads the exact byte value which is slightly more than 100m
if [[ "$(($user_used/1024/1024))m" != "$file_size" ]]; then
	log_note "user $QUSER1 used is $user_used"
	log_fail "userused for user $QUSER1 expected to be $file_size, " \
	    "not $user_used"
fi

if [[ $user_used != $group_used ]]; then
	log_note "user $QUSER1 used is $user_used"
	log_note "group $QGROUP used is $group_used"
	log_fail "FAIL: userused should equal to groupused"
fi

log_pass "Check the basic function of {user|group}used pass as expect"
