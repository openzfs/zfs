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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify incremental receive properly handles objects with changed
# dnode slot count.
#
# Strategy:
# 1. Populate a dataset with 1k byte dnodes and snapshot
# 2. Remove objects, set dnodesize=legacy, and remount dataset so new objects
#    get recycled numbers and formerly "interior" dnode slots get assigned
#    to new objects
# 3. Remove objects, set dnodesize=2k, and remount dataset so new objects
#    overlap with recently recycled and formerly "normal" dnode slots get
#    assigned to new objects
# 4. Create an empty file and add xattrs to it to exercise reclaiming a
#    dnode that requires more than 1 slot for its bonus buffer (Zol #7433)
# 5. Generate initial and incremental streams
# 6. Verify initial and incremental streams can be received
#

verify_runnable "both"

log_assert "Verify incremental receive handles objects with changed dnode size"

function cleanup
{
	rm -f $BACKDIR/fs-dn-legacy
	rm -f $BACKDIR/fs-dn-1k
	rm -f $BACKDIR/fs-dn-2k
	rm -f $BACKDIR/fs-attr

	datasetexists $POOL/fs && destroy_dataset $POOL/fs -rR
	datasetexists $POOL/newfs && destroy_dataset $POOL/newfs -rR
}

log_onexit cleanup

# 1. Populate a dataset with 1k byte dnodes and snapshot
log_must zfs create -o dnodesize=1k $POOL/fs
log_must mk_files 200 262144 0 $POOL/fs
log_must zfs snapshot $POOL/fs@a

# 2. Remove objects, set dnodesize=legacy, and remount dataset so new objects
#    get recycled numbers and formerly "interior" dnode slots get assigned
#    to new objects
rm /$POOL/fs/*

log_must zfs unmount $POOL/fs
log_must zfs set dnodesize=legacy $POOL/fs
log_must zfs mount $POOL/fs

log_must mk_files 200 262144 0 $POOL/fs
log_must zfs snapshot $POOL/fs@b

# 3. Remove objects, set dnodesize=2k, and remount dataset so new objects
#    overlap with recently recycled and formerly "normal" dnode slots get
#    assigned to new objects
rm /$POOL/fs/*

log_must zfs unmount $POOL/fs
log_must zfs set dnodesize=2k $POOL/fs
log_must zfs mount $POOL/fs

log_must touch /$POOL/fs/attrs
mk_files 200 262144 0 $POOL/fs
log_must zfs snapshot $POOL/fs@c

# 4. Create an empty file and add xattrs to it to exercise reclaiming a
#    dnode that requires more than 1 slot for its bonus buffer (Zol #7433)
log_must zfs set compression=on xattr=sa $POOL/fs
log_must eval "python3 -c 'print \"a\" * 512' |
    set_xattr_stdin bigval /$POOL/fs/attrs"
log_must zfs snapshot $POOL/fs@d

# 5. Generate initial and incremental streams
log_must eval "zfs send $POOL/fs@a > $BACKDIR/fs-dn-1k"
log_must eval "zfs send -i $POOL/fs@a $POOL/fs@b > $BACKDIR/fs-dn-legacy"
log_must eval "zfs send -i $POOL/fs@b $POOL/fs@c > $BACKDIR/fs-dn-2k"
log_must eval "zfs send -i $POOL/fs@c $POOL/fs@d > $BACKDIR/fs-attr"

# 6. Verify initial and incremental streams can be received
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-dn-1k"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-dn-legacy"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-dn-2k"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-attr"

log_pass "Verify incremental receive handles objects with changed dnode size"
