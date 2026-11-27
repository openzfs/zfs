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
#       1. set a user quota and a default group quota on a fs
#       2. write some data to the fs with a specified user and group
#       3. check the permission gate: denied without delegation,
#          denied with a single delegation, allowed only with all
#          four family delegations, denied again after revocation,
#          and denied inside an unprivileged user namespace
#       4. use the .zfs/space and .zfs/quota files to check the result,
#          including the default group quota row, the quota/project
#          row provenance, and the reported file size
#

function cleanup
{
	zfs unallow $QUSER1 $QFS >/dev/null 2>&1
	log_must zfs set defaultgroupquota=none $QFS
	log_must cleanup_quota
	log_must restore_tunable CTLDIR_SPACEFILES
}

log_onexit cleanup

log_assert "Check the .zfs space/quota files"

typeset userquota=104857600
typeset groupdefault=268435456

log_must zfs set userquota@$QUSER1=$userquota $QFS
log_must zfs set defaultgroupquota=$groupdefault $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 50m $QFILE

log_must save_tunable CTLDIR_SPACEFILES
log_must set_tunable32 CTLDIR_SPACEFILES 1
typeset mntp=$(get_prop mountpoint $QFS)
typeset user_id=$(id -u $QUSER1) || log_fail
typeset group_id=$(id -g $QUSER1) || log_fail

sync_all_pools

#
# Permission gate: without delegation the reads must fail; a single
# delegation of the family is not enough.  The files require every
# delegation of the family (userused, userquota, userobjused,
# userobjquota), matching zfs userspace; with all four granted they
# must succeed; revoked, they fail again.
#
log_mustnot user_run $QUSER1 cat $mntp/.zfs/space/user
log_mustnot user_run $QUSER1 cat $mntp/.zfs/quota/user
log_must zfs allow $QUSER1 userused $QFS
log_mustnot user_run $QUSER1 cat $mntp/.zfs/space/user
log_mustnot user_run $QUSER1 cat $mntp/.zfs/quota/user
log_must zfs allow $QUSER1 userobjused $QFS
log_mustnot user_run $QUSER1 cat $mntp/.zfs/space/user
log_mustnot user_run $QUSER1 cat $mntp/.zfs/quota/user
log_must zfs unallow $QUSER1 $QFS
log_must zfs allow $QUSER1 userused,userquota,userobjused,userobjquota $QFS
log_must user_run $QUSER1 cat $mntp/.zfs/space/user
log_must user_run $QUSER1 cat $mntp/.zfs/quota/user
log_must zfs unallow $QUSER1 $QFS
log_mustnot user_run $QUSER1 cat $mntp/.zfs/space/user
log_mustnot user_run $QUSER1 cat $mntp/.zfs/quota/user

#
# A root created inside an unprivileged user namespace maps to an
# ordinary uid, so the space files must stay inaccessible there
# without delegation.  Skip where unprivileged user namespaces are
# restricted.
#
if user_run $QUSER1 unshare -Ur true; then
	log_mustnot user_run $QUSER1 unshare -Ur \
	    cat "$mntp/.zfs/space/user"
fi

#
# Row content.  quota/user fills the explicit userquota@$QUSER1;
# space/group shows the group usage; quota/group has no groupquota@
# set, so its column fills with defaultgroupquota on the row the
# GROUPUSED walk created for $group_id.
#
log_must eval "grep \"^$user_id,[[:digit:]]*\" $mntp/.zfs/space/user"
log_must eval "grep \"^$user_id,$userquota\" $mntp/.zfs/quota/user"
log_must eval "grep \"^$group_id,[[:digit:]]*\" $mntp/.zfs/space/group"
log_must eval "grep \"^$group_id,$groupdefault\" $mntp/.zfs/quota/group"

#
# Row provenance: quota files walk their own family's used
# properties, so a plain uid with usage must never appear in
# quota/project.
#
log_mustnot eval "grep \"^$user_id,\" $mntp/.zfs/quota/project"

#
# Reported size matches the rendered content (getattr measures the
# file rather than reporting the lookup placeholder).
#
typeset size=$(stat -c %s $mntp/.zfs/space/user)
typeset bytes=$(wc -c < $mntp/.zfs/space/user)
log_must [ "$size" -eq "$bytes" ]

log_pass "Check the .zfs space/quota files"
