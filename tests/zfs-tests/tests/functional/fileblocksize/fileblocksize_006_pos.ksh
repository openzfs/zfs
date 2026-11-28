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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

#
# DESCRIPTION:
#	Files created and written after an extent size setting replay from
#	the ZIL with the setting.
#
# STRATEGY:
#	1. Set inheritance on a directory and pin an empty file.
#	2. Freeze the pool, then create files and directories there and
#	   write them with fsync, so they exist only in the ZIL.
#	3. Export and import to replay, then check data and block sizes.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "global"

function cleanup
{
	rm -rf $TESTDIR/zil
}

log_onexit cleanup
log_assert "ZIL replay keeps fixed block sizes and inheritance"

log_must mkdir $TESTDIR/zil
log_must extsize -s 8192 $TESTDIR/zil
log_must touch $TESTDIR/zil/pinned
log_must extsize -s 4096 $TESTDIR/zil/pinned
# ZIL records are only written after a freeze if a ZIL header exists
log_must dd if=/dev/zero of=$TESTDIR/zil/hdr conv=fsync bs=1 count=1
sync_pool $TESTPOOL

log_must zpool freeze $TESTPOOL
log_must dd if=/dev/urandom of=$TESTDIR/zil/pinned bs=64k count=16 \
    conv=fsync 2>/dev/null
log_must dd if=/dev/urandom of=$TESTDIR/zil/new bs=64k count=16 \
    conv=fsync 2>/dev/null
log_must mkdir $TESTDIR/zil/sub
log_must dd if=/dev/urandom of=$TESTDIR/zil/sub/deep bs=64k count=16 \
    conv=fsync 2>/dev/null
typeset f
typeset -A sum
for f in pinned new sub/deep; do
	sum[$f]=$(xxh128digest $TESTDIR/zil/$f)
done

reimport_pool

for f in pinned new sub/deep; do
	[[ "$(xxh128digest $TESTDIR/zil/$f)" == "${sum[$f]}" ]] ||
	    log_fail "$f differs after replay"
done
verify_blksz $TESTDIR/zil/pinned 4096
verify_ext $TESTDIR/zil/new extsize 8192
verify_blksz $TESTDIR/zil/new 8192
verify_ext $TESTDIR/zil/sub extszinherit 8192
verify_blksz $TESTDIR/zil/sub/deep 8192

log_pass "ZIL replay keeps fixed block sizes and inheritance"
