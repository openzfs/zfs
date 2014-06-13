#! /usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
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

log_assert "The RBAC profile \"ZFS Storage Management\" works"

ZFS_USER=$($CAT /tmp/zfs-privs-test-user.txt)

# the user shouldn't be able to do anything initially
log_mustnot $SU $ZFS_USER -c "$ZPOOL create $TESTPOOL $DISKS"
log_mustnot $SU $ZFS_USER -c "$PFEXEC $ZPOOL create $TESTPOOL $DISKS"

# the first time we assign the profile, we insist it should work
log_must $USERMOD -P "ZFS Storage Management" $ZFS_USER
log_must $SU $ZFS_USER -c "$PFEXEC $ZPOOL create -f $TESTPOOL $DISKS"

# ensure the user can't create a filesystem with this profile
log_mustnot $SU $ZFS_USER -c "$ZFS create $TESTPOOL/fs"

# add ZFS File System Management profile, and try to create a fs
log_must $USERMOD -P "ZFS File System Management" $ZFS_USER
log_must $SU $ZFS_USER -c "$PFEXEC $ZFS create $TESTPOOL/fs"

# revoke File System Management profile
$USERMOD -P, $ZFS_USER
$USERMOD -P "ZFS Storage Management" $ZFS_USER

# ensure the user can destroy pools
log_mustnot $SU $ZFS_USER -c "$ZPOOL destroy $TESTPOOL"
log_must $SU $ZFS_USER -c "$PFEXEC $ZPOOL destroy $TESTPOOL"

# revoke Storage Management profile
$USERMOD -P, $ZFS_USER
log_mustnot $SU $ZFS_USER -c "$PFEXEC $ZPOOL create -f $TESTPOOL $DISKS"

log_pass "The RBAC profile \"ZFS Storage Management\" works"
