#!/bin/ksh
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
# Copyright (c) 2026 George Melikov
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify incremental receive handles a FREEOBJECTS record that starts on an
# interior slot of a multi-slot dnode the same stream freed just before.
#
# When a multi-slot dnode is freed on the sender and a smaller one is later
# allocated on one of its interior slots, the stream frees the leading
# slots, claims the new object and then frees the trailing slots as three
# separate records.  The receiver frees the whole dnode on the first one,
# but its interior slots stay marked as such until that free syncs, so the
# claim is deferred and the trailing FREEOBJECTS lands on an interior slot.
#
# Strategy:
# 1. Create files with 4k dnodes and send the full stream
# 2. Remove one of them and, with dnodesize=legacy, allocate objects on
#    the slots it occupied, keeping only those on its interior slots
# 3. Verify the incremental stream can be received and the result matches
#    the source
#

verify_runnable "both"

log_assert "Verify incremental receive handles frees over an interior slot"

function cleanup
{
	rm -f $BACKDIR/fs-full
	rm -f $BACKDIR/fs-incr

	datasetexists $POOL/fs && destroy_dataset $POOL/fs -rR
	datasetexists $POOL/newfs && destroy_dataset $POOL/newfs -rR
}

log_onexit cleanup

# 1. Create files with 4k dnodes and send the full stream
log_must zfs create -o dnodesize=4k -o xattr=sa $POOL/fs

typeset -i i
for (( i = 0; i < 3; i++ )); do
	log_must touch /$POOL/fs/old.$i
done
log_must sync_pool $POOL
typeset -i freed=$(get_objnum /$POOL/fs/old.1)

log_must zfs snapshot $POOL/fs@a
log_must eval "zfs send $POOL/fs@a > $BACKDIR/fs-full"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-full"

# 2. Remove one of them and, with dnodesize=legacy, allocate objects on
#    the slots it occupied, keeping only those on its interior slots
log_must zfs set dnodesize=legacy $POOL/fs
log_must rm /$POOL/fs/old.1
log_must sync_pool $POOL

#
# Object allocation continues from where it left off for as long as the
# objset stays open, so reopen it to make it start over from the lowest
# free slots.  The first allocation may take the head slot of the freed
# dnode and other CPUs allocate from other chunks of the object space, so
# keep the files that landed on its interior slots and retry if none did.
#
typeset -i attempt inside=0
for (( attempt = 0; attempt < 5 && inside == 0; attempt++ )); do
	log_must zpool export $POOL
	log_must zpool import $POOL

	for (( i = 0; i < 8; i++ )); do
		log_must touch /$POOL/fs/new.$i
	done
	log_must sync_pool $POOL

	for (( i = 0; i < 8; i++ )); do
		typeset -i obj=$(get_objnum /$POOL/fs/new.$i)
		if (( obj > freed && obj < freed + 8 )); then
			log_note "new.$i is object $obj, inside $freed"
			(( inside += 1 ))
		else
			log_must rm /$POOL/fs/new.$i
		fi
	done
	log_must sync_pool $POOL
done
(( inside > 0 )) || log_fail "No object landed on an interior slot of $freed"

log_must zfs snapshot $POOL/fs@b

# 3. Verify the incremental stream can be received and the result matches
#    the source
log_must eval "zfs send -i $POOL/fs@a $POOL/fs@b > $BACKDIR/fs-incr"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-incr"

log_must directory_diff /$POOL/fs /$POOL/newfs

log_pass "Verify incremental receive handles frees over an interior slot"
