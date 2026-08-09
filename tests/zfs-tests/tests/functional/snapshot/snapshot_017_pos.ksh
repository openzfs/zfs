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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#


#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
#
# Directory structure of snapshots reflects filesystem structure.
#
# STRATEGY:
#
# This test makes sure that the directory structure of snapshots is
# a proper reflection of the filesystem the snapshot was taken of.
#
# 1. Create a simple directory structure of files and directories
# 2. Take a snapshot of the filesystem
# 3. Modify original filesystem
# 4. Walk down the snapshot directory structure verifying it
#    checking with both absolute and relative paths
#

verify_runnable "both"

function cleanup
{
	cd $SAVED_DIR

	datasetexists $TESTPOOL/$TESTFS && \
		destroy_dataset $TESTPOOL/$TESTFS -Rf

	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

function verify_structure {

	# check absolute paths
	DIR=$PWD
	verify_file $DIR/file1
	verify_file $DIR/file2
	verify_file $DIR/dir1/file3
	verify_file $DIR/dir1/file4
	verify_file $DIR/dir1/dir2/file5
	verify_file $DIR/dir1/dir2/file6

	verify_no_file $DIR/file99

	# check relative paths
	verify_file ./file1
	verify_file ./file2
	verify_file ./dir1/file3
	verify_file ./dir1/file4
	verify_file ./dir1/dir2/file5
	verify_file ./dir1/dir2/file6

	cd dir1
	verify_file ../file1
	verify_file ../file2
	verify_file ./file3
	verify_file ./file4

	verify_no_file ../file99

	cd dir2
	verify_file ./file5
	verify_file ./file6
	verify_file ../file3
	verify_file ../file4
	verify_no_file ../file99

	verify_file ../../file1
	verify_file ../../file2
	verify_no_file ../../file99
}

function verify_file {
	if [ ! -e $1 ]
	then
		log_note "Working dir is $PWD"
		log_fail "File $1 does not exist!"
	fi
}

function verify_no_file {
	if [ -e $1 ]
	then
		log_note "Working dir is $PWD"
		log_fail "File $1 exists when it should not!"
	fi
}

function verify_dir {
	if [ ! -d $1 ]
	then
		log_note "Working dir is $PWD"
		log_fail "Directory $1 does not exist!"
	fi
}

log_assert "Directory structure of snapshots reflects filesystem structure."
log_onexit cleanup

SAVED_DIR=$PWD

#
# Create a directory structure with the following files
#
# ./file1
# ./file2
# ./dir1/file3
# ./dir1/file4
# ./dir1/dir2/file5
# ./dir1/dir2/file6

cd $TESTDIR
mkfile 10m file1
mkfile 20m file2
mkdir dir1
cd dir1
mkfile 10m file3
mkfile 20m file4
mkdir dir2
cd dir2
mkfile 10m file5
mkfile 20m file6

# Now walk the directory structure verifying it
cd $TESTDIR
verify_structure

# Take snapshots
log_must zfs snapshot $TESTPOOL/$TESTFS@snap_a
log_must zfs snapshot $TESTPOOL/$TESTFS@snap_b

# Change the filesystem structure by renaming files in the original structure
# The snapshot file structure should not change
cd $TESTDIR
log_must mv file2 file99
cd dir1
log_must mv file4 file99
cd dir2
log_must mv file6 file99

# verify the top level snapshot directories
verify_dir $TESTDIR/.zfs
verify_dir $TESTDIR/.zfs/snapshot
verify_dir $TESTDIR/.zfs/snapshot/snap_a
verify_dir $TESTDIR/.zfs/snapshot/snap_b

cd $TESTDIR/.zfs/snapshot/snap_a
verify_structure

cd $TESTDIR/.zfs/snapshot/snap_b
verify_structure

cd $TESTDIR/.zfs
verify_dir snapshot
cd $TESTDIR/.zfs/snapshot
verify_dir snap_a
verify_dir snap_b

cd snap_a
verify_dir ../snap_a
verify_dir ../snap_b

cd ..
verify_dir snap_a
verify_dir snap_b

log_pass "Directory structure of snapshots reflects filesystem structure."
