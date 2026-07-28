#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the License at usr/src/OPENSOLARIS.LICENSE.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
#
# CDDL HEADER END
#
#
# DESCRIPTION:
#	Verify that online rollback invalidates cached POSIX ACLs.
#
# STRATEGY:
#	1. Snapshot a file whose group mode bit requires an ACL check, but where
#	   the test user has no access.
#	2. Grant the test user read access so the VFS caches the newer ACL.
#	3. Keep the inode active over rollback, then verify the test user cannot
#	   read the recovered file without dropping inode or page caches.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_runnable "both"
log_assert "Online rollback invalidates cached POSIX ACLs"

typeset file=$TESTDIR/posix-acl-rollback
typeset snap=$TESTPOOL/$TESTFS@posix-acl-rollback
typeset user=$ZFS_ACL_STAFF1

function cleanup
{
	snapexists $snap && destroy_dataset $snap -f
	rm -f $file
}

log_onexit cleanup

log_must touch $file
# Keep a group read bit so generic_permission() consults a cached POSIX ACL.
# The test user is not in root's group, so this still denies the initial read.
log_must chmod 640 $file
log_mustnot user_run $user cat $file
log_must zfs snapshot $snap

# setfacl updates the VFS ACL cache before rollback.
log_must setfacl -m u:$user:r-- $file
log_must user_run $user cat $file

# Online rollback shrinks the dcache.  Hold the inode alive so the following
# pathname lookup reuses its VFS ACL cache instead of allocating a new inode.
log_must eval "exec 9< $file"
log_must zfs rollback $snap
log_mustnot user_run $user cat $file
log_must eval "exec 9>&-"

log_pass "Online rollback invalidates cached POSIX ACLs"
