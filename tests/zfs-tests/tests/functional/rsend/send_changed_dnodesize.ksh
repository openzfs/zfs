#!/bin/ksh

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
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify incremental receive properly handles objects with changed
# dnode size.
#
# Strategy:
# 1. Populate a dataset with dnodesize=auto with objects
# 2. Take a snapshot
# 3. Remove objects and set dnodesize=legacy
# 4. Remount dataset so object numbers get recycled and formerly
#    "interior" dnode slots get assigned to new objects
# 5. Repopulate dataset
# 6. Send first snapshot to new dataset
# 7. Send incremental snapshot from second snapshot to new dataset
#

verify_runnable "both"

log_assert "Verify incremental receive handles objects with changed dnode size"

function cleanup
{
	rm -f $BACKDIR/fs-dn-auto
	rm -f $BACKDIR/fs-dn-legacy
}

log_onexit cleanup

log_must zfs create -o dnodesize=auto $POOL/fs
mk_files 200 262144 0 $POOL/fs
log_must zfs unmount $POOL/fs
log_must zfs snapshot $POOL/fs@a
log_must zfs mount $POOL/fs
log_must rm /$POOL/fs/*
log_must zfs unmount $POOL/fs
log_must zfs set dnodesize=legacy $POOL/fs
log_must zfs mount $POOL/fs
mk_files 200 262144 0 $POOL/fs
log_must zfs unmount $POOL/fs
log_must zfs snapshot $POOL/fs@b
log_must eval "zfs send $POOL/fs@a > $BACKDIR/fs-dn-auto"
log_must eval "zfs send -i $POOL/fs@a $POOL/fs@b > $BACKDIR/fs-dn-legacy"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-dn-auto"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs-dn-legacy"

log_pass "Verify incremental receive handles objects with changed dnode size"
