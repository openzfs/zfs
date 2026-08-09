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
# Copyright (c) 2025, iXsystems, Inc.
#

# DESCRIPTION:
#	Verify zfs rewrite rewrites specified files blocks.
#
# STRATEGY:
#	1. Create two files, one of which is in a directory.
#	2. Save the checksums and block pointers.
#	3. Rewrite part of the files.
#	4. Verify checksums are the same.
#	5. Verify block pointers of the rewritten part have changed.
#	6. Rewrite all the files.
#	7. Verify checksums are the same.
#	8. Verify all block pointers have changed.

. $STF_SUITE/include/libtest.shlib

typeset tmp=$(mktemp)
typeset bps=$(mktemp)
typeset bps1=$(mktemp)
typeset bps2=$(mktemp)

function cleanup
{
	rm -rf $tmp $bps $bps1 $bps2 $TESTDIR/*
}

log_assert "zfs rewrite rewrites specified files blocks"

log_onexit cleanup

log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

log_must mkdir $TESTDIR/dir
log_must dd if=/dev/urandom of=$TESTDIR/file1 bs=128k count=8
log_must dd if=$TESTDIR/file1 of=$TESTDIR/dir/file2 bs=128k
log_must sync_pool $TESTPOOL
typeset orig_hash1=$(xxh128digest $TESTDIR/file1)
typeset orig_hash2=$(xxh128digest $TESTDIR/dir/file2)

log_must [ "$orig_hash1" = "$orig_hash2" ]
log_must eval "zdb -Ovv $TESTPOOL/$TESTFS file1 > $tmp"
log_must eval "awk '/ L0 / { print l++ \" \" \$3 }' < $tmp > $bps1"
log_must eval "zdb -Ovv $TESTPOOL/$TESTFS dir/file2 > $tmp"
log_must eval "awk '/ L0 / { print l++ \" \" \$3 }' < $tmp > $bps2"

log_must zfs rewrite -o 327680 -l 262144 -r -x $TESTDIR/file1 $TESTDIR/dir/file2
log_must sync_pool $TESTPOOL
typeset new_hash1=$(xxh128digest $TESTDIR/file1)
typeset new_hash2=$(xxh128digest $TESTDIR/dir/file2)
log_must [ "$orig_hash1" = "$new_hash1" ]
log_must [ "$orig_hash2" = "$new_hash2" ]

log_must eval "zdb -Ovv $TESTPOOL/$TESTFS file1 > $tmp"
log_must eval "awk '/ L0 / { print l++ \" \" \$3 }' < $tmp > $bps"
typeset same=$(echo $(sort -n $bps $bps1 | uniq -d | cut -f1 -d' '))
log_must [ "$same" = "0 1 5 6 7" ]
log_must eval "zdb -Ovv $TESTPOOL/$TESTFS dir/file2 > $tmp"
log_must eval "awk '/ L0 / { print l++ \" \" \$3 }' < $tmp > $bps"
typeset same=$(echo $(sort -n $bps $bps2 | uniq -d | cut -f1 -d' '))
log_must [ "$same" = "0 1 5 6 7" ]

log_must zfs rewrite -r $TESTDIR/file1 $TESTDIR/dir/file2
log_must sync_pool $TESTPOOL
typeset new_hash1=$(xxh128digest $TESTDIR/file1)
typeset new_hash2=$(xxh128digest $TESTDIR/dir/file2)
log_must [ "$orig_hash1" = "$new_hash1" ]
log_must [ "$orig_hash2" = "$new_hash2" ]

log_must eval "zdb -Ovv $TESTPOOL/$TESTFS file1 > $tmp"
log_must eval "awk '/ L0 / { print l++ \" \" \$3 }' < $tmp > $bps"
typeset same=$(echo $(sort -n $bps $bps1 | uniq -d | cut -f1 -d' '))
log_must [ -z "$same" ]
log_must eval "zdb -Ovv $TESTPOOL/$TESTFS dir/file2 > $tmp"
log_must eval "awk '/ L0 / { print l++ \" \" \$3 }' < $tmp > $bps"
typeset same=$(echo $(sort -n $bps $bps2 | uniq -d | cut -f1 -d' '))
log_must [ -z "$same" ]

log_pass
