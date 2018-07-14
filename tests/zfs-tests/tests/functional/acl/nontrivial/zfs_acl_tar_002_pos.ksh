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

#
# Copyright (c) 2016, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
# Verify that 'tar' command with -p@ option supports to archive ZFS ACLs
#	& xattrs
#
# STRATEGY:
# 1. Create file and directory in zfs filesystem
# 2. Add new ACE in ACL of file and directory
# 3. Create xattr of the file and directory
# 4. Use tar cf@ to archive file and directory
# 5. Use tar xf@ to extract the archive file
# 6. Verify that the restored ACLs & xttrs of file and directory identify
#    with the origional ones.
#

verify_runnable "both"

function cleanup
{
	if datasetexists $TESTPOOL/$TESTFS1; then
		log_must zfs destroy -f $TESTPOOL/$TESTFS1
	fi

	(( ${#cwd} != 0 )) && cd $cwd
	log_must rm -rf $TESTDIR1 $TESTDIR/* $mytestfile
}

log_assert "Verify that 'tar' command supports to archive ZFS ACLs & xattrs."

log_onexit cleanup

set -A ops " A+user:other1:add_file:allow" "A+everyone@:execute:allow" "a-x" \
    "777"
mytestfile=$(mktemp -t file.XXXX)
log_must dd if=/dev/urandom of=$mytestfile bs=1024k count=1
log_must chmod 644 $mytestfile

TARFILE=tarfile.$$.tar
cwd=$PWD

log_note "Create second zfs file system to restore the tar archive."
log_must zfs create $TESTPOOL/$TESTFS1
[[ ! -d $TESTDIR1 ]] && \
	log_must mkdir -p $TESTDIR1
log_must zfs set mountpoint=$TESTDIR1 $TESTPOOL/$TESTFS1

log_note "Create a file: $testfile, and directory: $testdir, in zfs " \
    "filesystem. And prepare for there xattr files."

for user in root $ZFS_ACL_STAFF1; do
	# Set the current user
	log_must set_cur_usr $user

	# Create source object and target directroy
	cd $TESTDIR
	log_must usr_exec touch $testfile
	log_must usr_exec mkdir $testdir

	log_must usr_exec runat $testfile cp $mytestfile attr.0
	log_must usr_exec runat $testdir cp $mytestfile attr.0

	# Add the new ACE on the head.
	log_note "Change the ACLs of file and directory with " \
		"'chmod ${ops[0]}'."
	log_must usr_exec chmod ${ops[0]} $testfile
	log_must usr_exec chmod ${ops[0]} $testdir

	log_note "Archive the file and directory."
	log_must tar cpf@ $TARFILE ${testfile#$TESTDIR/} ${testdir#$TESTDIR/}

	log_note "Restore the tar archive."
	cd $TESTDIR1
	log_must tar xpf@ $TESTDIR/$TARFILE

	log_note "Verify the ACLs of restored file/directory have no changes."
	for obj in $testfile $testdir; do
		log_must compare_modes $obj $TESTDIR1/${obj##*/}
		log_must compare_acls $obj $TESTDIR1/${obj##*/}
		log_must compare_xattrs $obj $TESTDIR1/${obj##*/}
	done

	log_must rm -rf $TESTDIR/* $TESTDIR1/*
done

log_pass "'tar' command succeeds to support ZFS ACLs."
