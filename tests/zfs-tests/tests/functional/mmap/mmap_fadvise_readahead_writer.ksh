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
#	fadvise(WILLNEED) readahead on a mapped file and pwrite() to the same
#	file must not wait for each other.  The kernel hands ->readahead()
#	its pages locked.  If readahead then waits for the range lock of a
#	writer that waits in update_pages() for one of those pages, neither
#	moves again, and the pool stops syncing behind the writer.
#
# STRATEGY:
#	1. Use 4K records, so that the range lock of a write covers exactly
#	   the pages it writes.
#	2. Run four writers rewriting a 64M file and four readers that drop
#	   a span of it from the page cache, fault one page of the span back
#	   in through a MADV_RANDOM mapping and ask for the span with
#	   fadvise(WILLNEED), for RUN_S seconds.
#	3. Fail if neither side moves for STALL_S seconds, or if the helper
#	   is still running TIMEOUT_S seconds later.  On a module that
#	   hangs here, the pool stays hung after the test has failed.
#

verify_runnable "global"

typeset NAME=mmap_fadvise_readahead_writer
typeset TESTFILE=$TESTDIR/$NAME.$$
typeset OUT=$TEST_BASE_DIR/$NAME.out.$$
typeset -i RUN_S=30
typeset -i STALL_S=10
typeset -i TIMEOUT_S=$((RUN_S + STALL_S + 60))
typeset helper_pid=""

function cleanup
{
	if [[ -n "$helper_pid" ]]; then
		kill -9 $helper_pid 2>/dev/null
	fi
	log_must zfs inherit recordsize $TESTPOOL/$TESTFS
	rm -f $TESTFILE $OUT
}

log_assert "fadvise(WILLNEED) readahead and writes to a mapped file" \
    "keep making progress"
log_onexit cleanup

log_must zfs set recordsize=4k $TESTPOOL/$TESTFS

$NAME $TESTFILE $RUN_S $STALL_S >$OUT 2>&1 &
helper_pid=$!

typeset -i waited=0
while kill -0 $helper_pid 2>/dev/null && ((waited < TIMEOUT_S)); do
	sleep 1
	((waited++))
done
if kill -0 $helper_pid 2>/dev/null; then
	log_fail "helper still running after ${TIMEOUT_S}s: $(<$OUT)"
fi

wait $helper_pid
typeset -i rc=$?
helper_pid=""
log_note "$(<$OUT)"

case $rc in
0)
	log_pass "readahead and writes kept making progress"
	;;
2)
	log_fail "readahead and writes stopped making progress"
	;;
*)
	log_fail "$NAME failed with status $rc"
	;;
esac
