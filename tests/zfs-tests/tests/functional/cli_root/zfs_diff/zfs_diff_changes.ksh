#!/bin/ksh -p
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
# 'zfs diff' should display changes correctly.
#
# STRATEGY:
# 1. Create a filesystem with both files and directories, then snapshot it
# 2. Generate different types of changes and verify 'zfs diff' displays them
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -r "$DATASET"
	rm -f "$FILEDIFF"
}

#
# Verify object $path has $change type
# Valid types are:
# * - (The path has been removed)
# * + (The path has been created)
# * M (The path has been modified)
# * R (The path has been renamed)
#
function verify_object_change # <path> <change>
{
	path="$1"
	change="$2"

	log_must eval "zfs diff -F $TESTSNAP1 $TESTSNAP2 > $FILEDIFF"
	diffchg="$(awk -v path="$path" '$NF == path { print $1 }' $FILEDIFF)"
	if [[ "$diffchg" != "$change" ]]; then
		log_fail "Unexpected change for $path ('$diffchg' != '$change')"
	else
		log_note "Object $path change is displayed correctly: '$change'"
	fi
}

log_assert "'zfs diff' should display changes correctly."
log_onexit cleanup

DATASET="$TESTPOOL/$TESTFS/fs"
TESTSNAP1="$DATASET@snap1"
TESTSNAP2="$DATASET@snap2"
FILEDIFF="$TESTDIR/zfs-diff.txt"

# 1. Create a filesystem with both files and directories, then snapshot it
log_must zfs create $DATASET
MNTPOINT="$(get_prop mountpoint $DATASET)"
log_must touch "$MNTPOINT/fremoved"
log_must touch "$MNTPOINT/frenamed"
log_must touch "$MNTPOINT/fmodified"
log_must mkdir "$MNTPOINT/dremoved"
log_must mkdir "$MNTPOINT/drenamed"
log_must mkdir "$MNTPOINT/dmodified"
log_must zfs snapshot "$TESTSNAP1"

# 2. Generate different types of changes and verify 'zfs diff' displays them
log_must rm -f "$MNTPOINT/fremoved"
log_must mv "$MNTPOINT/frenamed" "$MNTPOINT/frenamed.new"
log_must touch "$MNTPOINT/fmodified"
log_must rmdir "$MNTPOINT/dremoved"
log_must mv "$MNTPOINT/drenamed" "$MNTPOINT/drenamed.new"
log_must touch "$MNTPOINT/dmodified/file"
log_must touch "$MNTPOINT/fcreated"
log_must mkdir "$MNTPOINT/dcreated"
log_must zfs snapshot "$TESTSNAP2"
verify_object_change "$MNTPOINT/fremoved" "-"
verify_object_change "$MNTPOINT/frenamed.new" "R"
verify_object_change "$MNTPOINT/fmodified" "M"
verify_object_change "$MNTPOINT/fcreated" "+"
verify_object_change "$MNTPOINT/dremoved" "-"
verify_object_change "$MNTPOINT/drenamed.new" "R"
verify_object_change "$MNTPOINT/dmodified" "M"
verify_object_change "$MNTPOINT/dcreated" "+"

log_pass "'zfs diff' displays changes correctly."
