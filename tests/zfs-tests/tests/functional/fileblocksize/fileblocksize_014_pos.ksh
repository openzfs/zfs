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
#	zfs fileblocksize -r and zfs project -r don't follow symbolic links
#	or open FIFOs below the top.
#
# STRATEGY:
#	1. Make a tree with a FIFO and symbolic links to a file and a
#	   directory outside it.
#	2. Set a block size and a project ID on the tree with -r, bounded by
#	   a timeout, and check that the link targets are unchanged.
#	3. Check that a link named on the command line is followed.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset t=$TESTDIR/walk
typeset o=$TESTDIR/outside

function cleanup
{
	rm -rf $t $o
}

log_onexit cleanup
log_assert "-r doesn't follow symbolic links or open FIFOs"

log_must mkdir -p $t/sub $o/dir
log_must touch $t/sub/empty $o/file
log_must mkfifo $t/sub/fifo
log_must ln -s $o/file $t/sub/filelink
log_must ln -s $o/dir $t/dirlink

log_must timeout 60 zfs fileblocksize -b 16K -r $t
verify_ext $t/sub/empty extsize 16384
verify_ext $t/sub extszinherit 16384
verify_ext $o/file none 0
verify_ext $o/dir none 0

log_must timeout 60 zfs project -p 9 -s -r $t
out=$(zfs project $o/file)
[[ "$out" == *" 0 - $o/file" || "$out" == "0 - $o/file" ]] ||
    log_fail "project ID set through a link: '$out'"
out=$(zfs project -d $o/dir)
[[ "$out" == *" 0 - $o/dir" || "$out" == "0 - $o/dir" ]] ||
    log_fail "project ID set through a link: '$out'"
log_must timeout 60 zfs fileblocksize -c -r $t
log_must timeout 60 zfs project -c -r $t

log_must touch $o/dir/in
log_must timeout 60 zfs fileblocksize -b 8K -r $t/dirlink
verify_ext $o/dir extszinherit 8192
verify_ext $o/dir/in extsize 8192

log_pass "-r doesn't follow symbolic links or open FIFOs"
