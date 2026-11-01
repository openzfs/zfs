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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Zeroing part of a page that readahead left unfilled must not mark
#	that page up to date with the rest of it stale.  On Linux before
#	6.14, read_pages() keeps the pages ->readahead() did not take in
#	the page cache, not up to date.  zfs_zero_partial_page() zeroes part
#	of a cached page when a write extends the file past its last page,
#	and when a hole is punched inside a page.
#
# STRATEGY:
#	1. Use 128K records, so that a file of 8292 bytes is a single block
#	   of 8704 bytes, and no compression.
#	2. For each of extend and punch, ROUNDS times:
#	   a. Write a new file of 8292 bytes, and drop its pages by
#	      exporting and importing the pool.
#	   b. Touch its first page through a mapping, fadvise(WILLNEED) the
#	      whole file, then write a byte at 20000 (extend) or punch a
#	      hole at [8200, 8250) (punch).  Read the last page of the old
#	      file with pread() and compare it with what the file holds.
#	   c. Export and import the pool, then compare the whole file.
#	3. After all the rounds, fail if any read in b was wrong.  A wrong
#	   read in c fails at once.
#

verify_runnable "global"

typeset NAME=mmap_fadvise_readahead_zero_partial
typeset TESTFILE=$TESTDIR/$NAME.$$
typeset -i ROUNDS=10

function cleanup
{
	poolexists $TESTPOOL || log_must zpool import $TESTPOOL
	log_must zfs inherit compression $TESTPOOL/$TESTFS
	log_must zfs inherit recordsize $TESTPOOL/$TESTFS
	rm -f $TESTFILE
}

function drop_pages
{
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
}

log_assert "zeroing part of a page that readahead left unfilled leaves" \
    "no stale bytes"
log_onexit cleanup

log_must zfs set recordsize=128k $TESTPOOL/$TESTFS
log_must zfs set compression=off $TESTPOOL/$TESTFS

typeset -i seed=0
typeset -i failed=0
for mode in extend punch; do
	typeset -i round=1
	typeset -i wrong=0
	while ((round <= ROUNDS)); do
		((seed++))
		log_note "$mode: round $round of $ROUNDS"
		log_must $NAME create $TESTFILE $seed
		drop_pages
		$NAME test $mode $TESTFILE $seed
		typeset -i rc=$?
		if ((rc == 1)); then
			((wrong++))
		elif ((rc != 0)); then
			log_fail "$NAME test $mode failed with status $rc"
		fi
		drop_pages
		log_must $NAME cold $mode $TESTFILE $seed
		log_must rm $TESTFILE
		((round++))
	done
	log_note "$mode: $wrong of $ROUNDS rounds read wrong bytes"
	((failed += wrong))
done

((failed == 0)) || log_fail "$failed of $((2 * ROUNDS)) rounds read wrong bytes"

log_pass "zeroing part of a page that readahead left unfilled leaves" \
    "no stale bytes"
