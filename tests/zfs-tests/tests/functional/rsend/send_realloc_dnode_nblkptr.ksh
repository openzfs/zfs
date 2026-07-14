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
# Copyright (c) 2026 by Connectwise
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify incremental receive of multi-slot (large) dnodes with non-SA bonus
# buffers does not mis-detect reallocation via deduce_nblkptr().
#
# The rsend/send_realloc_{files,encrypted_files,dnode_size} tests exercise
# reallocation with modern SA bonuses. For DMU_OT_SA, deduce_nblkptr()
# always returns 1, matching allocation, so those tests cannot catch a
# slots-unaware deduce_nblkptr() that still used DN_OLD_MAX_BONUSLEN.
#
# Pre-SA filesystems store znodes as DMU_OT_ZNODE with a fixed 0x108-byte
# bonus. On a multi-slot dnode that yields nblkptr=3 at  allocate time,
# while the old deduce formula returned 1. On non-raw  incremental receive
# that made receive_handle_existing_object() free still-needed object
# contents.
#
# Strategy:
# 1. Create a version=4 filesystem with dnodesize=1k (non-SA, multi-slot).
# 2. Write a multi-block file, snapshot, and fully receive it.
# 3. Overwrite only the first record (OBJECT + partial WRITEs), snapshot,
#    and incrementally receive; verify contents match.
#

verify_runnable "both"

log_assert "Incremental receive preserves large-dnode non-SA object contents"

typeset sendfile=$BACKDIR/fs-nblkptr

function cleanup
{
	rm -f $sendfile $sendfile.incr
	datasetexists $POOL/fs && destroy_dataset $POOL/fs -rR
	datasetexists $POOL/newfs && destroy_dataset $POOL/newfs -rR
}

log_onexit cleanup

#
# Underestimated nblkptr must not free unchanged blocks on incr receive.
#
log_must zfs create -o version=4 -o dnodesize=1k -o recordsize=128k $POOL/fs
log_must dd if=/dev/urandom of=/$POOL/fs/file bs=128k count=8
log_must zfs snapshot $POOL/fs@a
log_must eval "zfs send $POOL/fs@a > $sendfile"
log_must eval "zfs recv $POOL/newfs < $sendfile"
log_must zfs set atime=off $POOL/newfs

log_must dd if=/dev/urandom of=/$POOL/fs/file bs=128k count=1 conv=notrunc
expected_cksum=$(recursive_cksum /$POOL/fs)
log_must zfs snapshot $POOL/fs@b
log_must eval "zfs send -i $POOL/fs@a $POOL/fs@b > $sendfile.incr"
log_must eval "zfs recv $POOL/newfs < $sendfile.incr"

actual_cksum=$(recursive_cksum /$POOL/newfs)
if [[ "$expected_cksum" != "$actual_cksum" ]]; then
	log_fail "Checksums differ ($expected_cksum != $actual_cksum)"
fi

log_pass "Incremental receive preserves large-dnode non-SA object contents"
