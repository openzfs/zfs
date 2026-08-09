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
# Test that POSIX_FADV_DONTNEED evicts data from the ZFS dbuf cache.
#
# STRATEGY:
# 1. Write blocks to a file and sync, so they land in the dbuf LRU cache
# 2. Record cache_count from dbufstats
# 3. Call file_fadvise with POSIX_FADV_DONTNEED on the file
# 4. Verify that cache_count decreased
# 5. Sanity-check eviction for single-block files.
#

verify_runnable "global"

FILE0=$TESTDIR/$TESTFILE0
FILE1=$TESTDIR/$TESTFILE1
BLKSZ=$(get_prop recordsize $TESTPOOL)

function cleanup
{
	[[ -e $TESTDIR ]] && log_must rm -Rf $TESTDIR/*
}

log_assert "Ensure POSIX_FADV_DONTNEED evicts data from the dbuf cache"

log_onexit cleanup

log_must file_write -o create -f $FILE0 -b $BLKSZ -c 100
sync_pool $TESTPOOL

evicts1=$(kstat dbufstats.cache_count)

log_must file_fadvise -f $FILE0 -a POSIX_FADV_DONTNEED

evicts2=$(kstat dbufstats.cache_count)
log_note "cache_count before=$evicts1 after=$evicts2"

log_must [ $evicts1 -gt $evicts2 ]

log_must file_write -o create -f $FILE1 -b 12000 -c 1
sync_pool $TESTPOOL

log_must file_fadvise -f $FILE1 -a POSIX_FADV_DONTNEED

log_pass "POSIX_FADV_DONTNEED evicts data from the dbuf cache"
