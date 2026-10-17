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
# Copyright (c) 2024 by Sam Atkinson.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the zfs space/quota files in the .zfs directory
#
#
# STRATEGY:
#       1. set zfs userquota/groupquota on a fs
#       2. write some data to the fs with a specified user and group
#       3. use the .zfs/space and .zfs/quota files to check the result
#

typeset SPACEFILES_ENABLED_PARAM=/sys/module/zfs/parameters/zfs_ctldir_spacefiles

function cleanup
{
	log_must cleanup_quota
	echo 0 >$SPACEFILES_ENABLED_PARAM || log_fail
}

log_onexit cleanup

log_assert "Check the .zfs space/quota files"

typeset userquota=104857600
typeset groupquota=524288000

log_must zfs set userquota@$QUSER1=$userquota $QFS
log_must zfs set groupquota@$QGROUP=$groupquota $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 50m $QFILE

echo 1 >$SPACEFILES_ENABLED_PARAM || log_fail
typeset mntp=$(get_prop mountpoint $QFS)
typeset user_id=$(id -u $QUSER1) || log_fail
typeset group_id=$(id -g $QUSER1) || log_fail

sync_all_pools

log_must eval "grep \"^$user_id,[[:digit:]]*\" $mntp/.zfs/space/user"
log_must eval "grep \"^$user_id,$userquota\" $mntp/.zfs/quota/user"
log_must eval "grep \"^$group_id,[[:digit:]]*\" $mntp/.zfs/space/group"
log_must eval "grep \"^$group_id,$groupquota\" $mntp/.zfs/quota/group"

log_pass "Check the .zfs space/quota files"
