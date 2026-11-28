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

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	'zfs project' stays inside the tree it is given: it doesn't follow
#	symbolic links or open FIFOs it finds there, and isn't led out by a
#	directory swapped for a symbolic link during the walk.
#
# STRATEGY:
#	1. Make a tree with a FIFO and symbolic links to a file and a
#	   directory outside it.  Set a project ID with -r, and list a
#	   directory without -r, each bounded by a timeout.  Check that the
#	   link targets are unchanged.
#	2. Ten times, walk a tree with -r while a background loop keeps
#	   swapping a subdirectory for a symbolic link to a directory outside
#	   it, and check that the walk ends and the outside directory is
#	   unchanged.  The walk may report the swapped directory.  This walk
#	   is quick, so the race is rarely hit; fileblocksize_016_pos, whose
#	   walk waits for a txg per file, covers it with a wide window.
#

verify_runnable "both"

typeset o=$TESTDIR/outside
typeset stop=$TESTDIR/swap.stop
typeset pid=""

function cleanup
{
	touch $stop
	[[ -n "$pid" ]] && wait $pid
	rm -rf $PRJDIR $o $stop
}

# The project ID of a file, or of a directory itself.
function projid # path
{
	typeset d=""

	[[ -d $1 ]] && d=-d
	zfs project $d $1 | awk '{print $1}'
}

function check_projid # path id
{
	typeset got=$(projid $1)

	[[ "$got" == "$2" ]] || log_fail "$1 has project ID '$got', not $2"
}

log_onexit cleanup
log_assert "'zfs project' stays inside the tree it is given"

# 1. Links and FIFOs inside the tree.
log_must mkdir -p $PRJDIR/sub $o/dir
log_must touch $PRJDIR/sub/file $o/file
log_must mkfifo $PRJDIR/sub/fifo
log_must ln -s $o/file $PRJDIR/sub/filelink
log_must ln -s $o/dir $PRJDIR/dirlink
log_must timeout 60 zfs project -s -p 9 -r $PRJDIR
check_projid $PRJDIR/sub/file 9
check_projid $o/file 0
check_projid $o/dir 0
log_must eval "timeout 60 zfs project $PRJDIR/sub > $TESTDIR/list"
log_must grep -q "sub/file" $TESTDIR/list
log_mustnot grep -qE "fifo|link" $TESTDIR/list
log_must timeout 60 zfs project -C -r $PRJDIR
log_must rm -rf $PRJDIR/*

# 2. A directory swapped for a symbolic link while the walk is queued.
for i in $(seq 1 30); do
	log_must touch $PRJDIR/f$i
done
for i in $(seq 1 10); do
	typeset a=$PRJDIR/a$i

	log_must mkdir $a
	log_must touch $a/in
	rm -f $stop
	(
		while [[ ! -e $stop ]]; do
			mv $a $a.x && ln -s $o $a
			sleep 0.05
			rm -f $a && mv $a.x $a
			sleep 0.05
		done
	) &
	pid=$!
	# One project ID throughout: a rename into a directory with
	# another inherited ID fails with EXDEV and would stop the swaps.
	timeout 60 zfs project -s -p 101 -r $PRJDIR
	(( $? == 124 )) && log_fail "the walk hung"
	touch $stop
	wait $pid
	pid=""
	check_projid $o 0
	check_projid $o/file 0
done
log_must rm -rf $PRJDIR/*

log_pass "'zfs project' stays inside the tree it is given"
