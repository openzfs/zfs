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
# Verify that '$TAR' command with -p option supports to archive ZFS ACLs
#
# STRATEGY:
# 1. Create file and directory in zfs filesystem
# 2. Add new ACE in ACL of file and directory
# 3. Use $TAR to archive file and directory
# 4. Extract the archive file
# 5. Verify that the restored ACLs of file and directory identify
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

log_assert "Verify that '$TAR' command supports to archive ZFS ACLs."

log_onexit cleanup

set -A ops " A+user:other1:add_file:allow" "A+everyone@:execute:allow" "a-x" "777"

TARFILE=tarfile.$$.tar
file=$TESTFILE0
dir=dir.$$
cwd=$PWD

log_note "Create second zfs file system to restore the tar archive."
log_must $ZFS create $TESTPOOL/$TESTFS1
[[ ! -d $TESTDIR1 ]] && \
	log_must $MKDIR -p $TESTDIR1
log_must $ZFS set mountpoint=$TESTDIR1 $TESTPOOL/$TESTFS1

log_note "Create a file: $file, and directory: $dir, in zfs filesystem. "
cd $TESTDIR
log_must $TOUCH $file
log_must $MKDIR $dir

typeset -i i=0
while (( i < ${#ops[*]} ))
do
	log_note "Change the ACLs of file and directory with " \
		"'$CHMOD ${ops[i]}'."
	cd $TESTDIR
	for obj in $file $dir; do
		log_must $CHMOD ${ops[i]} $obj
	done
	log_note "Archive the file and directory."
	log_must $TAR $pack_preserve $TARFILE $file $dir

	log_note "Restore the tar archive."
	log_must $MV $TARFILE $TESTDIR1
	cd $TESTDIR1
	log_must $TAR $unpack_preserve $TARFILE

	log_note "Verify the ACLs of restored file/directory have no changes."
	for obj in $file $dir; do
		log_must compare_modes $TESTDIR/$obj $TESTDIR1/$obj
		log_must compare_acls $TESTDIR/$obj $TESTDIR1/$obj
	done

	log_must $RM -rf $TESTDIR1/*

	(( i = i + 1 ))
done

log_pass "'$TAR' command succeeds to support ZFS ACLs."
