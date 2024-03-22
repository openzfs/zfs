#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the zfs space files in .zfs directory
#
#
# STRATEGY:
#       1. set zfs userquota to a fs
#       2. write some data to the fs with specified user and group
#       3. use zfs space files in .zfs to check the result
#

function cleanup
{
	log_must cleanup_quota
	echo 0 >$SPACEFILES_ENABLED_PARAM || log_fail
}

log_onexit cleanup

log_assert "Check the .zfs space files"

typeset userquota=104857600
typeset groupquota=524288000

log_must zfs set userquota@$QUSER1=$userquota $QFS
log_must zfs set groupquota@$QGROUP=$groupquota $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 50m $QFILE

typeset SPACEFILES_ENABLED_PARAM=/sys/module/zfs/parameters/zfs_ctldir_spacefiles
echo 1 >$SPACEFILES_ENABLED_PARAM || log_fail
typeset mntp=$(get_prop mountpoint $QFS)
typeset user_id=$(id -u $QUSER1) || log_fail
typeset group_id=$(id -g $QUSER1) || log_fail

sync_all_pools

log_must eval "grep \"^$user_id,[[:digit:]]*\" $mntp/.zfs/space/user"
log_must eval "grep \"^$user_id,$userquota\" $mntp/.zfs/quota/user"
log_must eval "grep \"^$group_id,[[:digit:]]*\" $mntp/.zfs/space/group"
log_must eval "grep \"^$group_id,$groupquota\" $mntp/.zfs/quota/group"

log_pass "Check the .zfs space files"

