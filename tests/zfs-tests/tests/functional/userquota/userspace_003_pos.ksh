#!/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Jinshan Xiong. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the user used object accounting in zfs userspace
#
#
# STRATEGY:
#       1. create a bunch of files by specific users
#	2. use zfs userspace to check the used objects
#	3. change the owner of test files and verify object count
#	4. delete files and verify object count
#

function cleanup
{
	datasetexists $snapfs && destroy_dataset $snapfs

	log_must rm -f ${QFILE}_*
	log_must cleanup_quota
}

function user_object_count
{
	typeset fs=$1
	typeset user=$2
	typeset -i userspacecnt=$(zfs userspace -oname,objused $fs |
	    awk /$user/'{print $2}')
	typeset -i zfsgetcnt=$(zfs get -H -ovalue userobjused@$user $fs)

	# 'zfs userspace' and 'zfs get userobjused@' should be equal
	verify_eq "$userspacecnt" "$zfsgetcnt" "userobjused@$user"

	echo $userspacecnt
}

log_onexit cleanup

log_assert "Check the zfs userspace object used"

mkmount_writable $QFS
log_must zfs set xattr=sa $QFS

((user1_cnt = RANDOM % 100 + 1))
((user2_cnt = RANDOM % 100 + 1))

log_must user_run $QUSER1 mkfiles ${QFILE}_1 $user1_cnt
log_must user_run $QUSER2 mkfiles ${QFILE}_2 $user2_cnt
sync_pool

typeset snapfs=$QFS@snap

log_must zfs snapshot $snapfs

log_must eval "zfs userspace $QFS >/dev/null 2>&1"
log_must eval "zfs userspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the user used objects in zfs userspace $fs"
	[[ $(user_object_count $fs $QUSER1) -eq $user1_cnt ]] ||
		log_fail "expected $user1_cnt"
	[[ $(user_object_count $fs $QUSER2) -eq $user2_cnt ]] ||
		log_fail "expected $user2_cnt"
done

log_note "change the owner of files"
log_must chown $QUSER2 ${QFILE}_1*
sync_pool

[[ $(user_object_count $QFS $QUSER1) -eq 0 ]] ||
	log_fail "expected 0 files for $QUSER1"

[[ $(user_object_count $snapfs $QUSER1) -eq $user1_cnt ]] ||
	log_fail "expected $user_cnt files for $QUSER1 in snapfs"

[[ $(user_object_count $QFS $QUSER2) -eq $((user1_cnt+user2_cnt)) ]] ||
	log_fail "expected $((user1_cnt+user2_cnt)) files for $QUSER2"

log_note "file removal"
log_must rm ${QFILE}_*
sync_pool

[[ $(user_object_count $QFS $QUSER2) -eq 0 ]] ||
        log_fail "expected 0 files for $QUSER2"

cleanup
log_pass "Check the zfs userspace object used"
