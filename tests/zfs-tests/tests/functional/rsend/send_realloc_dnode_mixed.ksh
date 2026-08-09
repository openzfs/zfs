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
# Copyright (c) 2026 MorganaFuture
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify incremental receive handles a wide dnode claim that expands over
# a dnode reaching past the end of the claimed slot range.
#
# send_realloc_dnode_size covers reallocation between uniform dnode sizes,
# where every object of a snapshot starts on the same alignment and a wider
# claim therefore always ends on a dnode boundary.  Mixing the sizes puts
# multi-slot dnodes at odd offsets, so a claim can end in the middle of one
# and only the expansion loop in receive_object() is left to free it.
#
# Strategy:
# 1. Populate a dataset with the dnode size cycling through legacy, 1k and
#    2k so that the multi-slot dnodes sit at differing alignments
# 2. Remove the objects and refill at 2k, then remove part of what was
#    written so that the reallocated objects are separated by holes and the
#    sender frees the slots between them
# 3. Generate the initial and incremental streams
# 4. Verify both can be received and the result matches the source
#

verify_runnable "both"

log_assert "Verify incremental receive handles claims over misaligned dnodes"

function cleanup
{
	rm -f $BACKDIR/fs-mixed
	rm -f $BACKDIR/fs-realloc

	datasetexists $POOL/fs && destroy_dataset $POOL/fs -rR
	datasetexists $POOL/newfs && destroy_dataset $POOL/newfs -rR
}

log_onexit cleanup

typeset -i nfiles=150
typeset -i i=0

# 1. Populate a dataset with the dnode size cycling through legacy, 1k and
#    2k so that the multi-slot dnodes sit at differing alignments
log_must zfs create $POOL/fs

while (( i < nfiles )); do
	case $((i % 3)) in
	0)	log_must zfs set dnodesize=legacy $POOL/fs ;;
	1)	log_must zfs set dnodesize=1k $POOL/fs ;;
	2)	log_must zfs set dnodesize=2k $POOL/fs ;;
	esac

	log_must touch /$POOL/fs/file.$i
	(( i += 1 ))
done

log_must zfs snapshot $POOL/fs@a

# 2. Remove the objects and refill at 2k, then remove part of what was
#    written so that the reallocated objects are separated by holes and the
#    sender frees the slots between them
log_must eval "rm -f /$POOL/fs/file.*"

log_must zfs unmount $POOL/fs
log_must zfs set dnodesize=2k $POOL/fs
log_must zfs mount $POOL/fs

i=0
while (( i < nfiles )); do
	log_must touch /$POOL/fs/new.$i
	(( i += 1 ))
done

i=0
while (( i < nfiles )); do
	if (( i % 3 == 0 )); then
		log_must rm -f /$POOL/fs/new.$i
	fi
	(( i += 1 ))
done

log_must zfs snapshot $POOL/fs@b

# 3. Generate the initial and incremental streams
log_must eval "zfs send $POOL/fs@a > $BACKDIR/fs-mixed"
log_must eval "zfs send -i $POOL/fs@a $POOL/fs@b > $BACKDIR/fs-realloc"

# 4. Verify both can be received and the result matches the source
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-mixed"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-realloc"

log_must directory_diff /$POOL/fs /$POOL/newfs

log_pass "Verify incremental receive handles claims over misaligned dnodes"
