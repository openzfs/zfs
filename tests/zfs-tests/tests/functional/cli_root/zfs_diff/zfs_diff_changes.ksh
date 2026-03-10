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

#
# JSON change type strings corresponding to text symbols
#
typeset -A CHANGE_JSON
CHANGE_JSON["-"]="removed"
CHANGE_JSON["+"]="added"
CHANGE_JSON["M"]="modified"
CHANGE_JSON["R"]="renamed"

#
# Verify object $path has $change type in JSON output and that each
# JSON line contains the required fields (change_type, inode, gen,
# file_type, path)
#
function verify_object_change_json # <path> <change>
{
	path="$1"
	change="$2"
	expected="${CHANGE_JSON[$change]}"
	relpath="${path#$MNTPOINT}"

	log_must eval "zfs diff -j $TESTSNAP1 $TESTSNAP2 > $FILEDIFF"

	# Every line must be valid JSON with required fields
	while IFS= read -r line; do
		echo "$line" | log_must jq -e '
			has("change_type") and
			has("mountpoint") and
			has("inode") and
			has("gen") and
			has("file_type") and
			has("path")' > /dev/null
	done < "$FILEDIFF"

	# Renamed entries must have changes.path
	if [[ "$change" == "R" ]]; then
		diffchg="$(jq -r --arg p "$relpath" \
		    'select(.path == $p) | .changes.path' \
		    "$FILEDIFF" 2>/dev/null | head -1)"
		[[ -n "$diffchg" ]] || log_fail \
		    "JSON: renamed entry for $path missing changes.path"
	fi

	# Find the entry for our path and verify change_type
	diffchg="$(jq -r --arg p "$relpath" \
	    'select(.path == $p) | .change_type' \
	    "$FILEDIFF" 2>/dev/null | head -1)"
	if [[ "$diffchg" != "$expected" ]]; then
		log_fail "JSON: unexpected change_type for $path ('$diffchg' != '$expected')"
	else
		log_note "JSON: object $path change_type is correct: '$diffchg'"
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
log_must touch "$MNTPOINT/flinked"
log_must mkdir "$MNTPOINT/dremoved"
log_must mkdir "$MNTPOINT/drenamed"
log_must mkdir "$MNTPOINT/dmodified"
log_must zfs snapshot "$TESTSNAP1"

# 2. Generate different types of changes and verify 'zfs diff' displays them
log_must rm -f "$MNTPOINT/fremoved"
log_must mv "$MNTPOINT/frenamed" "$MNTPOINT/frenamed.new"
log_must touch "$MNTPOINT/fmodified"
log_must ln "$MNTPOINT/flinked" "$MNTPOINT/flinked.hard"
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

verify_object_change_json "$MNTPOINT/fremoved" "-"
verify_object_change_json "$MNTPOINT/frenamed.new" "R"
verify_object_change_json "$MNTPOINT/fmodified" "M"
verify_object_change_json "$MNTPOINT/fcreated" "+"
verify_object_change_json "$MNTPOINT/dremoved" "-"
verify_object_change_json "$MNTPOINT/drenamed.new" "R"
verify_object_change_json "$MNTPOINT/dmodified" "M"
verify_object_change_json "$MNTPOINT/dcreated" "+"

# Verify changes.nlink {from, to} for the hard-linked file
log_must eval "zfs diff -j $TESTSNAP1 $TESTSNAP2 > $FILEDIFF"
nlink_from="$(jq -r --arg p "/flinked" \
    'select(.path == $p) | .changes.nlink.from' "$FILEDIFF" 2>/dev/null)"
nlink_to="$(jq -r --arg p "/flinked" \
    'select(.path == $p) | .changes.nlink.to' "$FILEDIFF" 2>/dev/null)"
[[ "$nlink_from" == "1" && "$nlink_to" == "2" ]] || \
    log_fail "JSON: expected nlink {from:1, to:2} for flinked, got from=$nlink_from to=$nlink_to"
log_note "JSON: flinked changes.nlink correct: from=$nlink_from to=$nlink_to"

# Verify changes.ctime {from, to} with -jt
log_must eval "zfs diff -jt $TESTSNAP1 $TESTSNAP2 > $FILEDIFF"

# Every entry must have changes.ctime with at least one of from/to
while IFS= read -r line; do
	echo "$line" | log_must jq -e 'has("changes") and (.changes | has("ctime"))' \
	    > /dev/null
done < "$FILEDIFF"

# modified entry must have both changes.ctime.from and changes.ctime.to
ctime_from="$(jq -r --arg p "/fmodified" \
    'select(.path == $p) | .changes.ctime.from' "$FILEDIFF" 2>/dev/null)"
ctime_to="$(jq -r --arg p "/fmodified" \
    'select(.path == $p) | .changes.ctime.to' "$FILEDIFF" 2>/dev/null)"
[[ -n "$ctime_from" && "$ctime_from" != "null" && \
    -n "$ctime_to" && "$ctime_to" != "null" ]] || \
    log_fail "JSON: fmodified missing changes.ctime from/to"
log_note "JSON: fmodified changes.ctime.from=$ctime_from changes.ctime.to=$ctime_to"

# added entry must have changes.ctime.to but not changes.ctime.from
ctime_from="$(jq -r --arg p "/fcreated" \
    'select(.path == $p) | .changes.ctime.from' "$FILEDIFF" 2>/dev/null)"
ctime_to="$(jq -r --arg p "/fcreated" \
    'select(.path == $p) | .changes.ctime.to' "$FILEDIFF" 2>/dev/null)"
[[ "$ctime_from" == "null" && -n "$ctime_to" && "$ctime_to" != "null" ]] || \
    log_fail "JSON: fcreated ctime should have to but not from"
log_note "JSON: fcreated changes.ctime.to=$ctime_to (no from, as expected)"

# removed entry must have changes.ctime.from but not changes.ctime.to
ctime_from="$(jq -r --arg p "/fremoved" \
    'select(.path == $p) | .changes.ctime.from' "$FILEDIFF" 2>/dev/null)"
ctime_to="$(jq -r --arg p "/fremoved" \
    'select(.path == $p) | .changes.ctime.to' "$FILEDIFF" 2>/dev/null)"
[[ -n "$ctime_from" && "$ctime_from" != "null" && "$ctime_to" == "null" ]] || \
    log_fail "JSON: fremoved ctime should have from but not to"
log_note "JSON: fremoved changes.ctime.from=$ctime_from (no to, as expected)"

log_pass "'zfs diff' displays changes correctly."
