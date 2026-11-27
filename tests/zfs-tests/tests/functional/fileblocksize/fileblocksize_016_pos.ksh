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
#	zfs fileblocksize -r stays in the tree when a directory it has
#	queued is replaced by a symbolic link before it is read.  Its walk
#	waits for a txg per file, so the window is wide.
#
# STRATEGY:
#	1. Make a tree with enough empty files that each walk waits for
#	   several txgs.
#	2. Ten times, add a new subdirectory and walk the tree with -r,
#	   alternating block sizes, while a background loop keeps swapping
#	   the subdirectory for a symbolic link to a directory outside the
#	   tree.  A new name each time lands at a different place in the
#	   directory's order.
#	3. Then the same with a queued directory two levels down whose
#	   parent is swapped for a link to a directory that has a
#	   subdirectory of the same name.
#	4. Check that each walk ends and that nothing outside was pinned.
#	   A walk may report the swapped directory.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset t=$TESTDIR/swap
typeset o=$TESTDIR/swapout
typeset stop=$TESTDIR/swap.stop
typeset pid=""

function cleanup
{
	touch $stop
	[[ -n "$pid" ]] && wait $pid
	rm -rf $t $o $stop
}

log_onexit cleanup
log_assert "-r doesn't leave the tree when a queued directory is swapped"

log_must mkdir -p $t $o
log_must touch $o/victim
for i in $(seq 1 20); do
	log_must touch $t/f$i
done

for i in $(seq 1 10); do
	typeset a=$t/a$i

	(( i % 2 )) && s=8K || s=16K
	log_must mkdir $a
	log_must touch $a/in
	rm -f $stop
	(
		while [[ ! -e $stop ]]; do
			mv $a $a.x && ln -s $o $a
			sleep 0.1
			rm -f $a && mv $a.x $a
			sleep 0.1
		done
	) &
	pid=$!
	timeout 120 zfs fileblocksize -b $s -r $t
	(( $? == 124 )) && log_fail "the walk hung"
	touch $stop
	wait $pid
	pid=""
	verify_ext $o/victim none 0
	verify_ext $o none 0
done

# A queued directory's parent swapped: $t/p/a is queued while the files in
# $t/p are set, then $t/p becomes a link to $o, which has an a/ too.
log_must rm -rf $t/a* $t/f*
log_must mkdir -p $t/p/a $o/a
log_must touch $t/p/a/in $o/a/victim
for i in $(seq 1 20); do
	log_must touch $t/p/g$i
done
for i in $(seq 1 5); do
	(( i % 2 )) && s=8K || s=16K
	rm -f $stop
	(
		while [[ ! -e $stop ]]; do
			mv $t/p $t/p.x && ln -s $o $t/p
			sleep 0.1
			rm -f $t/p && mv $t/p.x $t/p
			sleep 0.1
		done
	) &
	pid=$!
	timeout 120 zfs fileblocksize -b $s -r $t
	(( $? == 124 )) && log_fail "the walk hung"
	touch $stop
	wait $pid
	pid=""
	verify_ext $o/a/victim none 0
	verify_ext $o/a none 0
done

log_pass "-r doesn't leave the tree when a queued directory is swapped"
