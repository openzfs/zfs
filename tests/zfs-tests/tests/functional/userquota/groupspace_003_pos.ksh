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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the user used and groupspace object counts in zfs groupspace
#
#
# STRATEGY:
#	1. set zfs groupquota to a fs
#	2. create objects for different users in the same group
#	3. use zfs groupspace to check the object count
#

function cleanup
{
	datasetexists $snapfs && destroy_dataset $snapfs

	log_must rm -f ${QFILE}_*
	log_must cleanup_quota
}

function group_object_count
{
	typeset fs=$1
	typeset group=$2
	typeset -i groupspacecnt=$(zfs groupspace -oname,objused $fs |
	    awk /$group/'{print $2}')
	typeset -i zfsgetcnt=$(zfs get -H -ovalue groupobjused@$group $fs)

	# 'zfs groupspace' and 'zfs get groupobjused@' should be equal
	verify_eq "$groupspacecnt" "$zfsgetcnt" "groupobjused@$group"

	echo $groupspacecnt
}

log_onexit cleanup

log_assert "Check the zfs groupspace object used"

mkmount_writable $QFS
log_must zfs set xattr=sa $QFS

((user1_cnt = RANDOM % 100 + 1))
((user2_cnt = RANDOM % 100 + 1))
log_must user_run $QUSER1 mkfiles ${QFILE}_1 $user1_cnt
log_must user_run $QUSER2 mkfiles ${QFILE}_2 $user2_cnt
((grp_cnt = user1_cnt + user2_cnt))
sync_pool

typeset snapfs=$QFS@snap

log_must zfs snapshot $snapfs

log_must eval "zfs groupspace $QFS >/dev/null 2>&1"
log_must eval "zfs groupspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the object count in zfs groupspace $fs"
        [[ $(group_object_count $fs $QGROUP) -eq $grp_cnt ]] ||
                log_fail "expected $grp_cnt"
done

log_note "file removal"
log_must rm ${QFILE}_*
sync_pool

[[ $(group_object_count $QFS $QGROUP) -eq 0 ]] ||
        log_fail "expected 0 files for $QGROUP"

[[ $(group_object_count $snapfs $QGROUP) -eq $grp_cnt ]] ||
        log_fail "expected $grp_cnt files for $QGROUP"

cleanup
log_pass "Check the zfs groupspace object used pass as expect"
