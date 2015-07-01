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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright (c) 2012 by Marcelo Leal. All rights reserved.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
# Verify that '$TAR' command with -p@ option supports to archive ZFS ACLs
#	& xattrs
#
# STRATEGY:
# 1. Create file and directory in zfs filesystem
# 2. Add new ACE in ACL of file and directory
# 3. Create xattr of the file and directory
# 4. Use $TAR cf@ to archive file and directory
# 5. Use $TAR xf@ to extract the archive file
# 6. Verify that the restored ACLs & xttrs of file and directory identify
#    with the origional ones.
#

verify_runnable "both"

function cleanup
{
	destroy_dataset -f $TESTPOOL/$TESTFS1

	(( ${#cwd} != 0 )) && cd $cwd
	[[ -d $TESTDIR1 ]] && log_must $RM -rf $TESTDIR1
	[[ -d $TESTDIR/ ]] && log_must $RM -rf $TESTDIR/*
}

log_assert "Verify that '$TAR' command supports to archive ZFS ACLs & xattrs."

log_onexit cleanup

set -A ops " A+user:other1:add_file:allow" "A+everyone@:execute:allow" "a-x" \
    "777"
mytestfile=/kernel/drv/zfs

TARFILE=tarfile.$$.tar
cwd=$PWD

log_note "Create second zfs file system to restore the tar archive."
log_must $ZFS create $TESTPOOL/$TESTFS1
[[ ! -d $TESTDIR1 ]] && \
	log_must $MKDIR -p $TESTDIR1
log_must $ZFS set mountpoint=$TESTDIR1 $TESTPOOL/$TESTFS1

log_note "Create a file: $testfile, and directory: $testdir, in zfs " \
    "filesystem. And prepare for there xattr files."

for user in root $ZFS_ACL_STAFF1; do
	# Set the current user
	log_must set_cur_usr $user

	# Create source object and target directroy
	cd $TESTDIR
	log_must usr_exec $TOUCH $testfile
	log_must usr_exec $MKDIR $testdir

	log_must usr_exec $RUNAT $testfile $CP $mytestfile attr.0
	log_must usr_exec $RUNAT $testdir $CP $mytestfile attr.0

	# Add the new ACE on the head.
	log_note "Change the ACLs of file and directory with " \
		"'$CHMOD ${ops[0]}'."
	log_must usr_exec $CHMOD ${ops[0]} $testfile
	log_must usr_exec $CHMOD ${ops[0]} $testdir

	log_note "Archive the file and directory."
	log_must $TAR cpf@ $TARFILE ${testfile#$TESTDIR/} ${testdir#$TESTDIR/}

	log_note "Restore the tar archive."
	cd $TESTDIR1
	log_must $TAR xpf@ $TESTDIR/$TARFILE

	log_note "Verify the ACLs of restored file/directory have no changes."
	for obj in $testfile $testdir; do
		log_must compare_modes $obj $TESTDIR1/${obj##*/}
		log_must compare_acls $obj $TESTDIR1/${obj##*/}
		log_must compare_xattrs $obj $TESTDIR1/${obj##*/}
	done

	log_must $RM -rf $TESTDIR/* $TESTDIR1/*
done

log_pass "'$TAR' command succeeds to support ZFS ACLs."
