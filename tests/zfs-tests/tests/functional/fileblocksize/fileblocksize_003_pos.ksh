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
#	FS_XFLAG_EXTSZINHERIT on a directory gives new files in it that block
#	size, fixed, and new subdirectories the setting.
#
# STRATEGY:
#	1. Set 16K on a directory and check new files, made several ways,
#	   and new subdirectories three levels down.
#	2. Check that other object types can still be created there.
#	3. Check that a file moved in keeps its own block size.
#	4. Check that a file's own setting can be cleared and changed.
#	5. Clear the directory and check that new files grow to recordsize.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

function cleanup
{
	rm -rf $TESTDIR/inh $TESTDIR/outside
}

log_onexit cleanup
log_assert "Directories pass their extent size to new files and directories"

typeset dir=$TESTDIR/inh
log_must mkdir $dir
log_must extsize -s 16384 $dir
verify_ext $dir extszinherit 16384

write_mib $dir/dd
verify_ext $dir/dd extsize 16384
verify_blksz $dir/dd 16384
log_must touch $dir/touched
verify_ext $dir/touched extsize 16384
write_mib $TESTDIR/outside
log_must cp $TESTDIR/outside $dir/copied
verify_blksz $dir/copied 16384
log_must mv $TESTDIR/outside $dir/moved
verify_ext $dir/moved none 0
verify_blksz $dir/moved 131072

log_must mkdir -p $dir/a/b/c
for d in $dir/a $dir/a/b $dir/a/b/c; do
	verify_ext $d extszinherit 16384
done
write_mib $dir/a/b/c/deep
verify_blksz $dir/a/b/c/deep 16384

log_must ln -s dd $dir/symlink
log_must ln $dir/dd $dir/hardlink
log_must mkfifo $dir/fifo
verify_blksz $dir/hardlink 16384

# a new file's own setting can be cleared or changed while it's empty
log_must touch $dir/changed
log_must extsize -s 4096 $dir/changed
write_mib $dir/changed
verify_blksz $dir/changed 4096
log_must touch $dir/unpinned
log_must extsize -c $dir/unpinned
verify_ext $dir/unpinned none 0

# a subdirectory may use another size
log_must extsize -s 65536 $dir/a/b
write_mib $dir/a/b/other
verify_blksz $dir/a/b/other 65536

log_must extsize -c $dir
verify_ext $dir none 0
write_mib $dir/after
verify_ext $dir/after none 0
verify_blksz $dir/after 131072
verify_ext $dir/a extszinherit 16384

log_pass "Directories pass their extent size to new files and directories"
