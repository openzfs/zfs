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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

#
# DESCRIPTION:
# 'zfs destroy -t <type>' fails and destroys nothing when the type of
# the target is not one of the listed types, and rejects invalid types.
#
# STRATEGY:
# 1. Create a file system with children, snapshots, a clone and a
#    bookmark, and a volume.
# 2. Try to destroy each of them with -t naming only other types, with
#    and without -f, -n, -r, -R and -d.
# 3. Verify that every attempt exits with status 1 and reports the
#    mismatch, and that everything is still in place and mounted.
# 4. Verify that invalid and missing types exit with status 2 and that
#    a nonexistent target is still an error.
#

verify_runnable "both"

fs=$TESTPOOL/$TESTFS1
vol=$TESTPOOL/$TESTVOL
clone=$TESTPOOL/clone
all="$fs $fs/child $fs@a $fs/child@a $fs#bm $clone"
roots="$fs $clone"

function cleanup
{
	datasetexists $fs && destroy_dataset $fs -Rf
	datasetexists $vol && destroy_dataset $vol -Rf
	datasetexists $clone && destroy_dataset $clone -Rf
}

# Everything created below must be there, and the file system mounted.
function verify_intact
{
	typeset expected=$(printf '%s\n' $all | sort)
	typeset actual=$(zfs list -H -o name -t all -r $roots | sort)

	[[ $actual == $expected ]] || log_fail "unexpected datasets:" \
	    "$actual, expected $expected"
	log_must ismounted $fs
}

# destroy_fails <status> <message> <zfs destroy arguments>
function destroy_fails
{
	typeset status=$1
	typeset msg=$2
	typeset out
	typeset ret

	shift 2
	out=$(zfs destroy "$@" 2>&1)
	ret=$?
	[[ $ret == $status ]] || log_fail "'zfs destroy $*' exited with" \
	    "$ret, expected $status: $out"
	[[ $out == *"$msg"* ]] || log_fail "'zfs destroy $*' printed" \
	    "'$out', expected '$msg'"
}

# mismatch <target> <flags> <type> ...
function mismatch
{
	typeset target=$1
	typeset flags=$2
	typeset t

	shift 2
	for t in "$@"; do
		destroy_fails 1 "type does not match '$t'" $flags -t $t $target
	done
}

log_assert "'zfs destroy -t' fails if the type of the target is not listed"
log_onexit cleanup

log_must zfs create $fs
log_must zfs create $fs/child
log_must zfs snapshot -r $fs@a
log_must zfs bookmark $fs@a $fs#bm
log_must zfs clone $fs@a $clone
if is_global_zone; then
	log_must zfs create -V $VOLSIZE $fs/vol
	log_must zfs create -V $VOLSIZE $vol
	log_must zfs snapshot $vol@a
	all="$all $fs/vol $vol $vol@a"
	roots="$roots $vol"
fi
verify_intact

# Each type of target, with and without the options that destroy more.
for flags in "" -f -n -r -R; do
	mismatch $fs "$flags" snapshot,volume,bookmark
	mismatch $fs@a "$flags" fs,volume,bookmark
	mismatch $fs#bm "$flags" filesystem,snap,vol
	if is_global_zone; then
		mismatch $vol "$flags" fs,snapshot,bookmark
		mismatch $vol@a "$flags" filesystem,vol,bookmark
	fi
done
verify_intact

# Single types and aliases, ranges and lists of snapshots, deferral.
mismatch $fs "" snapshot snap volume vol bookmark
mismatch $fs@a "" filesystem vol
mismatch $fs#bm "" volume snapshot
mismatch $fs@a -d filesystem bookmark
mismatch $fs@a,b -r filesystem
mismatch $fs@a%b -r volume

# The pool itself is only ever a file system.
mismatch $TESTPOOL -r snapshot volume bookmark
verify_intact

# Invalid types are usage errors, however the list is formed.
for t in bogus "" , fs, ,fs fs,,vol Snapshot; do
	destroy_fails 2 "invalid type '" -t "$t" $fs
done
destroy_fails 2 "invalid type 'bogus'" -t fs,bogus $fs
destroy_fails 2 "invalid type 'bogus'" -t bogus $fs@a
destroy_fails 2 "invalid type 'bogus'" -t bogus $fs#bm

# A missing argument for -t, and missing or surplus targets.
destroy_fails 2 "usage:" -t
destroy_fails 2 "missing dataset argument" -t fs
destroy_fails 2 "too many arguments" -t fs $fs $fs/child

# A nonexistent target is still an error, whatever the type.
log_mustnot zfs destroy -t filesystem $fs/nonexistent
log_mustnot zfs destroy -t snapshot $fs@nonexistent
log_mustnot zfs destroy -t bookmark $fs#nonexistent
verify_intact

log_pass "'zfs destroy -t' fails if the type of the target is not listed"
