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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# The RBAC profile "ZFS Storage Management" works
#
# STRATEGY:
#	(create)
#	1. As a normal user, try to create a pool - which should fail.
#       2. Assign "ZFS Storage Management" profile, try to create pool again,
#	   which should succeed.
#
#	(works well with other ZFS profile tests)
#	3. Attempt to create a ZFS filesystem, which should fail.
#	4. Add the "ZFS File System Management" profile, attempt to create a FS
# 	   which should succeed.
#
#	(destroy)
#       5. Remove the FS profile, then attempt to destroy the pool, which
# 	   should succeed.
#	6. Remove the Storage profile, then attempt to recreate the pool, which
#	   should fail.
#

# We can only run this in the global zone
verify_runnable "global"

if is_linux || is_freebsd; then
	log_unsupported "Requires pfexec command"
fi

log_assert "The RBAC profile \"ZFS Storage Management\" works"

ZFS_USER=$(<$TEST_BASE_DIR/zfs-privs-test-user.txt)

# the user shouldn't be able to do anything initially
log_mustnot user_run $ZFS_USER "zpool create $TESTPOOL $DISKS"
log_mustnot user_run $ZFS_USER "pfexec zpool create $TESTPOOL $DISKS"

# the first time we assign the profile, we insist it should work
log_must usermod -P "ZFS Storage Management" $ZFS_USER
log_must user_run $ZFS_USER "pfexec zpool create -f $TESTPOOL $DISKS"

# ensure the user can't create a filesystem with this profile
log_mustnot user_run $ZFS_USER "zfs create $TESTPOOL/fs"

# add ZFS File System Management profile, and try to create a fs
log_must usermod -P "ZFS File System Management" $ZFS_USER
log_must user_run $ZFS_USER "pfexec zfs create $TESTPOOL/fs"

# revoke File System Management profile
usermod -P, $ZFS_USER
usermod -P "ZFS Storage Management" $ZFS_USER

# ensure the user can destroy pools
log_mustnot user_run $ZFS_USER "zpool destroy $TESTPOOL"
log_must user_run $ZFS_USER "pfexec zpool destroy $TESTPOOL"

# revoke Storage Management profile
usermod -P, $ZFS_USER
log_mustnot user_run $ZFS_USER "pfexec zpool create -f $TESTPOOL $DISKS"

log_pass "The RBAC profile \"ZFS Storage Management\" works"
