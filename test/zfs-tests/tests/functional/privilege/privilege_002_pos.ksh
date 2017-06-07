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
# The RBAC profile "ZFS File System Management" works
#
# STRATEGY:
#
#	The following actions are taken, both using profile execution (pfexec)
#	and without profile execution - we make sure that the latter should
#	always fail.
#
#	(create)
#	1. As a normal user, try to create a filesystem - which should fail.
#       2. Assign "ZFS File System Management" profile, try to create fs again,
#	   which should succeed.
#
#	(pools)
#	3. Ensure a user with this profile can't perform pool administration
#	   by attempting to destroy a pool.
#
#	(destroy)
#       5. Remove the FS profile, then attempt to destroy the fs, which
# 	   should fail.
#	6. Assign the FS profile, then attempt to destroy the fs, which
#	   should succeed.
#

verify_runnable "both"

log_assert "The RBAC profile \"ZFS File System Management\" works"

ZFS_USER=$($CAT /tmp/zfs-privs-test-user.txt)

# Set a $DATASET where we can create child files systems
if is_global_zone; then
	log_must $ZPOOL create -f $TESTPOOL $DISKS
	DATASET=$TESTPOOL
else
	DATASET=zonepool/zonectr0
fi

# A user shouldn't be able to create filesystems
log_mustnot $SU $ZFS_USER -c "$ZFS create $DATASET/zfsprivfs"

# Insist this invocation of usermod works
log_must $USERMOD -P "ZFS File System Management" $ZFS_USER

# Now try to create file systems as the user
log_mustnot $SU $ZFS_USER -c "$ZFS create $DATASET/zfsprivfs"
log_must $SU $ZFS_USER -c "$PFEXEC $ZFS create $DATASET/zfsprivfs"

# Ensure the user can't do anything to pools in this state:
log_mustnot $SU $ZFS_USER -c "$ZPOOL destroy $DATASET"
log_mustnot $SU $ZFS_USER -c "$PFEXEC $ZPOOL destroy $DATASET"

# revoke File System Management profile
$USERMOD -P, $ZFS_USER

# Ensure the user can't create more filesystems
log_mustnot $SU $ZFS_USER -c "$ZFS create $DATASET/zfsprivfs2"
log_mustnot $SU $ZFS_USER -c "$PFEXEC $ZFS create $DATASET/zfsprivfs2"

# assign the profile again and destroy the fs.
$USERMOD -P "ZFS File System Management" $ZFS_USER
log_must $SU $ZFS_USER -c "$PFEXEC $ZFS destroy $DATASET/zfsprivfs"
$USERMOD -P, $ZFS_USER

log_pass "The RBAC profile \"ZFS File System Management\" works"
