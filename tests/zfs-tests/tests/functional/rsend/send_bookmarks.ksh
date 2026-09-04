#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2026 by Andrew Mochalskyi. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	Verify 'zfs send --bookmarks' carries snapshot bookmarks and
#	'zfs receive -b' recreates them.
#
# STRATEGY:
#	1. Bookmark a snapshot, send it with --bookmarks and receive with -b;
#	   the bookmark is recreated with a matching guid.
#	2. Receiving the same stream without -b leaves no bookmark (opt-in).
#	3. Sending without --bookmarks carries nothing to recreate.
#	4. An incremental send --bookmarks recreates a bookmark of the
#	   incremental snapshot.
#	5. A raw send -w --bookmarks of an encrypted dataset recreates the
#	   bookmark with the receiver's own (non-zero) IV set guid.
#	6. A bookmark whose snapshot has been destroyed (an orphan) is not
#	   sent, and the receive still succeeds.
#

verify_runnable "both"

function cleanup
{
	for ds in $recvenc $enc $recv $send; do
		datasetexists $ds && log_must destroy_dataset "$ds" "-Rf"
	done
	[[ -e $stream ]] && log_must rm -f $stream
}

# Fail unless the named bookmark exists on the receiving side.
function bookmark_exists #<bookmark>
{
	zfs list -H -t bookmark -o name "$1" >/dev/null 2>&1
}

log_assert "'zfs send --bookmarks' and 'zfs receive -b' replicate bookmarks."
log_onexit cleanup

send=$TESTPOOL/sendbm
recv=$TESTPOOL/recvbm
enc=$TESTPOOL/encbm
recvenc=$TESTPOOL/recvencbm
stream=$TEST_BASE_DIR/bookmarks.$$

log_must zfs create $send
log_must zfs snapshot $send@snap1
log_must zfs bookmark $send@snap1 $send#mark1
src_guid=$(get_prop guid $send#mark1)

log_note "1. send --bookmarks + recv -b recreates the bookmark"
log_must eval "zfs send --bookmarks $send@snap1 > $stream"
log_must eval "zfs receive -b $recv <$stream"
log_must bookmark_exists $recv#mark1
log_must test "$(get_prop guid $recv#mark1)" = "$src_guid"

log_note "2. recv without -b ignores bookmarks in the stream"
log_must destroy_dataset "$recv" "-Rf"
log_must eval "zfs receive $recv <$stream"
log_mustnot bookmark_exists $recv#mark1

log_note "3. send without --bookmarks carries no bookmark"
log_must destroy_dataset "$recv" "-Rf"
log_must eval "zfs send $send@snap1 > $stream"
log_must eval "zfs receive -b $recv <$stream"
log_mustnot bookmark_exists $recv#mark1

log_note "4. incremental send --bookmarks recreates the new bookmark"
log_must destroy_dataset "$recv" "-Rf"
log_must eval "zfs send --bookmarks $send@snap1 > $stream"
log_must eval "zfs receive -b $recv <$stream"
log_must zfs snapshot $send@snap2
log_must zfs bookmark $send@snap2 $send#mark2
log_must eval "zfs send --bookmarks -i $send@snap1 $send@snap2 > $stream"
log_must eval "zfs receive -b $recv <$stream"
log_must bookmark_exists $recv#mark2
log_must test "$(get_prop guid $recv#mark2)" = "$(get_prop guid $send#mark2)"

log_note "5. raw send -w --bookmarks recreates the bookmark with an ivset guid"
log_must eval "echo 'password' | zfs create -o encryption=on" \
    " -o keyformat=passphrase -o keylocation=prompt $enc"
log_must zfs snapshot $enc@snap1
log_must zfs bookmark $enc@snap1 $enc#mark1
log_must eval "zfs send -w -p --bookmarks $enc@snap1 > $stream"
log_must eval "zfs receive -b $recvenc <$stream"
log_must bookmark_exists $recvenc#mark1
ivset=$(get_prop ivsetguid $recvenc#mark1)
log_must test -n "$ivset"
log_mustnot test "$ivset" = "0"
log_must destroy_dataset "$recvenc" "-Rf"
log_must destroy_dataset "$enc" "-Rf"

log_note "6. an orphan bookmark is skipped and receive still succeeds"
log_must destroy_dataset "$recv" "-Rf"
log_must zfs snapshot $send@snap3
log_must zfs bookmark $send@snap3 $send#orphan
log_must zfs destroy $send@snap3
log_must eval "zfs send -R --bookmarks $send@snap2 > $stream"
log_must eval "zfs receive -b $recv <$stream"
log_must bookmark_exists $recv#mark1
log_mustnot bookmark_exists $recv#orphan

log_pass "'zfs send --bookmarks' and 'zfs receive -b' replicate bookmarks."
