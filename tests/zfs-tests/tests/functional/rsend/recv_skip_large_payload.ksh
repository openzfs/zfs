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

#
# Copyright (c) 2026 by ConnectWise. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that zfs recv -n checks the payload size of a skipped record: at
# most SPA_MAXBLOCKSIZE, or for a DRR_BEGIN record, whose payload is a
# packed nvlist, at most the nvlist limit.
#
# Strategy:
# 1. Load each prebuilt stream.  Each has the DRR_BEGIN record for a
#    zfs send, then one record with a large payload, then DRR_END:
#    - object, write, spill, embedded: DRR_OBJECT (bonus), DRR_WRITE,
#      DRR_SPILL and DRR_WRITE_EMBEDDED records whose payload is
#      16 MiB (SPA_MAXBLOCKSIZE) + 64 KiB.
#    - write-max: a DRR_WRITE whose payload is exactly 16 MiB.
#    - begin: a nested DRR_BEGIN whose payload is 16 MiB + 64 KiB.
#    - begin-nvlist: a nested DRR_BEGIN whose payload is
#      64 MiB + 64 KiB.
#    - first-begin, first-begin-nvlist: no extra record; instead the
#      first DRR_BEGIN's payload is 16 MiB + 64 KiB and
#      64 MiB + 64 KiB respectively.
# 2. With the nvlist limit set to 32 MiB, confirm zfs recv -n rejects
#    the object, write, spill, embedded, begin-nvlist and
#    first-begin-nvlist streams with "invalid record payload length".
# 3. Confirm zfs recv -n reads the write-max, begin and first-begin
#    streams to the end: it succeeds, reports what it would receive and
#    prints no error.
#

verify_runnable "both"

typeset dir=$STF_SUITE/tests/functional/rsend
typeset stream=$BACKDIR/recv-skip-large.zsend
typeset err=$BACKDIR/recv-skip-large.err
typeset dst=$POOL/skipdst

function cleanup
{
	datasetexists $dst && destroy_dataset $dst -r
	rm -f $stream $err
	unset ZFS_SENDRECV_MAX_NVLIST
}
log_onexit cleanup

log_assert "zfs recv checks the payload size of skipped records"

# Set a limit between the (first-)begin and (first-)begin-nvlist payload
# sizes, so the result does not depend on the default (64 MiB).
export ZFS_SENDRECV_MAX_NVLIST=32M
log_must mkdir -p $BACKDIR
for kind in object write spill embedded begin-nvlist first-begin-nvlist; do
	log_must eval "bzcat <$dir/recv-skip-large-$kind.zsend.bz2 >$stream"
	log_mustnot eval "zfs recv -n -v $dst@s <$stream >$err 2>&1"
	log_must grep -q "cannot receive.*: invalid record payload length" $err
	log_mustnot datasetexists $dst
done

for kind in write-max begin first-begin; do
	log_must eval "bzcat <$dir/recv-skip-large-$kind.zsend.bz2 >$stream"
	log_must eval "zfs recv -n -v $dst@s <$stream >$err 2>&1"
	log_must grep -q "would receive full stream of .* into $dst@s" $err
	log_mustnot grep -q "cannot receive" $err
	log_mustnot datasetexists $dst
done

log_pass "zfs recv checks the payload size of skipped records"
