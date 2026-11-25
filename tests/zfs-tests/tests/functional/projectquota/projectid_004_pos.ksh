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
# Copyright (c) 2026 by Matt Turner. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Every object type inherits the project ID, and renaming within a
#	project directory is always allowed
#
#
# STRATEGY:
#	1. Create a regular file and a symlink in a directory, then set the
#	   directory's project ID with the inherit flag, leaving the two
#	   objects carrying no project ID of their own
#	2. Rename each of them within that directory, which has to be allowed
#	   whatever project ID they carry
#	3. Rename a regular file and a symlink created after the directory was
#	   tagged, and replace a symlink with "ln -sfn", which creates the new
#	   symlink under a temporary name and renames it into place
#	4. Check that a new symlink and a new FIFO are accounted to the
#	   directory's project, which they only are if they inherited its
#	   project ID
#	5. Rename a symlink into a different directory carrying the same
#	   project ID, which rename(2) only permits if the symlink carries
#	   that project ID too
#	6. Check that a rename into a different project is still refused,
#	   and that a symlink carrying no project ID cannot be renamed into
#	   one, which shows the check in step 5 is reached and enforced
#

function cleanup
{
	log_must rm -rf $PRJDIR1 $PRJDIR2 $PRJDIR3 $TESTDIR/tlink6
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

# renameat2(1) is used throughout rather than mv(1), which falls back to
# copying on EXDEV and would report success either way.
if ! renameat2 -C; then
	log_unsupported "renameat2 not supported on this (pre-3.15) linux kernel"
fi

log_onexit cleanup

log_assert "Every object type inherits the project ID, and renaming within" \
    "a project directory is always allowed"

log_must mkdir $PRJDIR1

#
# Objects created before the directory was tagged carry no project ID of
# their own, as every symlink on an existing pool does. This is the case
# the same-directory exemption exists for: renaming within one directory
# cannot move an object between projects, so it has to be allowed whatever
# project ID the object carries -- including none at all.
#
log_must touch $PRJDIR1/tofile1
log_must ln -s target $PRJDIR1/tolink1

log_must chattr +P -p $PRJID1 $PRJDIR1

log_must renameat2 $PRJDIR1/tofile1 $PRJDIR1/tofile2
log_must renameat2 $PRJDIR1/tolink1 $PRJDIR1/tolink2

# Objects created after the directory was tagged inherit its project ID, and
# renaming those within it has to keep working too.
log_must touch $PRJDIR1/tfile1
log_must renameat2 $PRJDIR1/tfile1 $PRJDIR1/tfile2

log_must ln -s target $PRJDIR1/tlink1
log_must renameat2 $PRJDIR1/tlink1 $PRJDIR1/tlink2

log_must ln -sfn other $PRJDIR1/tlink2
log_must eval '[[ $(readlink $PRJDIR1/tlink2) == other ]]'

# A subdirectory inherits both the project ID and the inherit flag, so it
# has to behave the same way.
log_must mkdir $PRJDIR1/subdir
log_must ln -s target $PRJDIR1/subdir/tlink3
log_must renameat2 $PRJDIR1/subdir/tlink3 $PRJDIR1/subdir/tlink4

#
# A symlink and a FIFO are not regular files or directories, and used to be
# left at the default project ID. Creating one now adds an object to the
# directory's project.
#
sync_pool
typeset prj_bef=$(project_obj_count $QFS $PRJID1)

log_must ln -s target $PRJDIR1/tlink5
log_must mkfifo $PRJDIR1/tfifo1

sync_pool
typeset prj_aft=$(project_obj_count $QFS $PRJID1)

[[ $prj_aft -eq $((prj_bef + 2)) ]] ||
	log_fail "new value ($prj_aft) is NOT 2 larger than old one ($prj_bef)"

#
# rename(2) into a different directory is allowed only when the project IDs
# match, so this passes only if the symlink really did inherit one.
#
log_must mkdir $PRJDIR2
log_must chattr +P -p $PRJID1 $PRJDIR2
log_must renameat2 $PRJDIR1/tlink5 $PRJDIR2/tlink5

# Crossing into a different project is still refused.
log_must mkdir $PRJDIR3
log_must chattr +P -p $PRJID2 $PRJDIR3
log_mustnot renameat2 $PRJDIR2/tlink5 $PRJDIR3/tlink5

# And a symlink created outside any project carries no project ID, so it
# cannot be renamed into one. This is what makes the check above meaningful:
# it shows the cross-project test is reached and enforced, so the rename that
# succeeded did so because the symlink had inherited a project ID.
log_must ln -s target $TESTDIR/tlink6
log_mustnot renameat2 $TESTDIR/tlink6 $PRJDIR1/tlink6
log_must rm -f $TESTDIR/tlink6

log_pass "Every object type inherits the project ID, and renaming within" \
    "a project directory is always allowed"
