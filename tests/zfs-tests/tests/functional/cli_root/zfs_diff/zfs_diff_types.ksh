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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs diff -F' shows different object types correctly.
#
# STRATEGY:
# 1. Prepare a dataset
# 2. Create different objects and verify 'zfs diff -F' shows the correct type
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -r "$DATASET"
	rm -f "$FILEDIFF"
}

#
# Verify object at $path is of type $symbol using 'zfs diff -F'
# Valid types are:
# * B (Block device)
# * C (Character device)
# * / (Directory)
# * > (Door)
# * | (Named pipe)
# * @ (Symbolic link)
# * P (Event port)
# * = (Socket)
# * F (Regular file)
#
function verify_object_class # <path> <symbol>
{
	path="$1"
	symbol="$2"

	log_must eval "zfs diff -F $TESTSNAP1 $TESTSNAP2 > $FILEDIFF"
	diffsym="$(awk -v path="$path" '$NF == path { print $2 }' $FILEDIFF)"
	if [[ "$diffsym" != "$symbol" ]]; then
		log_fail "Unexpected type for $path ('$diffsym' != '$symbol')"
	else
		log_note "Object $path type is correctly displayed as '$symbol'"
	fi

	log_must zfs destroy "$TESTSNAP1"
	log_must zfs destroy "$TESTSNAP2"
}

log_assert "'zfs diff -F' should show different object types correctly."
log_onexit cleanup

DATASET="$TESTPOOL/$TESTFS/fs"
TESTSNAP1="$DATASET@snap1"
TESTSNAP2="$DATASET@snap2"
FILEDIFF="$TESTDIR/zfs-diff.txt"
if is_freebsd; then
	MAJOR=$(stat -f %Hr /dev/null)
	MINOR=$(stat -f %Lr /dev/null)
else
	MAJOR=$(stat -c %t /dev/null)
	MINOR=$(stat -c %T /dev/null)
fi

# 1. Prepare a dataset
log_must zfs create $DATASET
MNTPOINT="$(get_prop mountpoint $DATASET)"
log_must zfs set devices=on $DATASET
log_must zfs set xattr=sa $DATASET

# 2. Create different objects and verify 'zfs diff -F' shows the correct type
# 2. F (Regular file)
log_must zfs snapshot "$TESTSNAP1"
log_must touch "$MNTPOINT/file"
log_must zfs snapshot "$TESTSNAP2"
verify_object_class "$MNTPOINT/file" "F"

# 2. @ (Symbolic link)
log_must zfs snapshot "$TESTSNAP1"
log_must ln -s "$MNTPOINT/file" "$MNTPOINT/link"
log_must zfs snapshot "$TESTSNAP2"
verify_object_class "$MNTPOINT/link" "@"

# 2. B (Block device)
log_must zfs snapshot "$TESTSNAP1"
log_must mknod "$MNTPOINT/bdev" b $MAJOR $MINOR
log_must zfs snapshot "$TESTSNAP2"
verify_object_class "$MNTPOINT/bdev" "B"

# 2. C (Character device)
log_must zfs snapshot "$TESTSNAP1"
log_must mknod "$MNTPOINT/cdev" c $MAJOR $MINOR
log_must zfs snapshot "$TESTSNAP2"
verify_object_class "$MNTPOINT/cdev" "C"

# 2. | (Named pipe)
log_must zfs snapshot "$TESTSNAP1"
log_must mkfifo "$MNTPOINT/fifo"
log_must zfs snapshot "$TESTSNAP2"
verify_object_class "$MNTPOINT/fifo" "|"

# 2. / (Directory)
log_must zfs snapshot "$TESTSNAP1"
log_must mkdir "$MNTPOINT/dir"
log_must zfs snapshot "$TESTSNAP2"
verify_object_class "$MNTPOINT/dir" "/"

# 2. = (Socket)
log_must zfs snapshot "$TESTSNAP1"
log_must zfs_diff-socket "$MNTPOINT/sock"
log_must zfs snapshot "$TESTSNAP2"
verify_object_class "$MNTPOINT/sock" "="

log_pass "'zfs diff -F' shows different object types correctly."
