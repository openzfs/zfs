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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that a spill block which is stored as an embedded block pointer is
# still sent as a DRR_SPILL record.
#
# Creating an embedded spill at runtime depends on metadata compression (not
# the dataset compression property) and is not reliable across platforms, so
# this test imports a tiny pre-built pool from rsend/embspill.dat.bz2.
#
# Strategy:
# 1. Import the canned pool and confirm the file's spill BP is EMBEDDED.
# 2. Send with -e, and verify the stream has a DRR_SPILL record for the
#    object and no DRR_WRITE_EMBEDDED record for it.
# 3. Verify the received copy has identical contents and xattrs.
#

verify_runnable "both"

typeset srcpool=embspill
typeset srcfs=$srcpool/fs
typeset recvfs=$POOL2/recvfs
typeset vdev=$TEST_BASE_DIR/embspill.dat
typeset stream=$BACKDIR/stream
typeset dump=$BACKDIR/dump
typeset zdbout=$BACKDIR/zdb.out
typeset bz2=$STF_SUITE/tests/functional/rsend/embspill.dat.bz2

function cleanup
{
	poolexists $srcpool && destroy_pool $srcpool
	datasetexists $recvfs && destroy_dataset $recvfs "-r"
	rm -f $vdev $stream $dump $zdbout
}

log_assert "Embedded spill blocks are sent as DRR_SPILL records"
log_onexit cleanup

log_must bzcat $bz2 >$vdev
log_must zpool import -d $TEST_BASE_DIR $srcpool

typeset senddir=$(get_prop mountpoint $srcfs)
typeset file=$senddir/spill
typeset -i obj=$(get_objnum $file)

log_must eval "zdb -ddddd $srcfs $obj >$zdbout"
log_must grep -q "Spill block:.*EMBEDDED" $zdbout

log_must eval "zfs send -e $srcfs@snap >$stream"
log_must eval "zstream dump -v <$stream >$dump"

log_must grep -q "SPILL block for object = $obj " $dump

typeset bad=$(awk -v obj=$obj \
    '$1 == "WRITE_EMBEDDED" && $4 == obj { print }' $dump)
[[ -z $bad ]] || log_fail "spill block sent as WRITE_EMBEDDED: $bad"

log_must eval "zfs recv $recvfs <$stream"

typeset expected=$(recursive_cksum $senddir)
typeset actual=$(recursive_cksum $(get_prop mountpoint $recvfs))
[[ "$expected" == "$actual" ]] || \
    log_fail "Checksums differ ($expected != $actual)"

log_pass "Embedded spill blocks are sent as DRR_SPILL records"
