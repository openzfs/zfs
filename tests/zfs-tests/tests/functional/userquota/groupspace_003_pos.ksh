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
sync_all_pools

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
