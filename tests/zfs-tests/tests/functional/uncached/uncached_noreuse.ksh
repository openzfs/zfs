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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that I/O on a descriptor marked POSIX_FADV_NOREUSE returns correct
# data and leaves none of the file's data blocks in the dbuf cache.
#
# Both platforms reach that state by different routes.  On Linux the hint is
# visible to ZFS as FMODE_NOREUSE before the I/O, so the data is never cached
# in the first place.  On FreeBSD the kernel issues a POSIX_FADV_DONTNEED
# after each I/O, which evicts the same blocks after the fact, so the copy
# still happens but nothing is left behind.
#
# STRATEGY:
# 1. Write a file through a descriptor marked POSIX_FADV_NOREUSE, then read
#    it back the same way, then read it back without the hint, then write it
#    again without the hint.
# 2. After each step count the level 0 dbufs the file left in the dbuf cache.
#    Counting per object rather than watching dbufstats.cache_count keeps the
#    result independent of whatever else on the system is using ZFS.
# 3. The hinted steps must leave nothing behind and the unhinted ones must
#    not, or the checks would pass for the wrong reason.  The reads also
#    verify the pattern that was written.
#

verify_runnable "global"

# POSIX_FADV_NOREUSE is only recorded as FMODE_NOREUSE since Linux 6.3.
# Before that the hint is stored nowhere and cannot reach ZFS.
if is_linux && [ $(linux_version) -lt $(linux_version "6.3") ]; then
	log_unsupported "POSIX_FADV_NOREUSE needs Linux 6.3 or newer"
fi

FILE=$TESTDIR/$TESTFILE0
BLKSZ=$(get_prop recordsize $TESTPOOL)
COUNT=100
DBUFS_FILE=$(mktemp -t dbufs.out.XXXXXX)

function cleanup
{
	log_must rm -f $FILE $DBUFS_FILE
}

function cached_dbufs
{
	typeset obj=$(get_objnum $FILE)

	kstat dbufs > $DBUFS_FILE
	dbufstat -bxn -i $DBUFS_FILE -F "object=$obj,level=0,dbc=1" | wc -l
}

log_assert "POSIX_FADV_NOREUSE I/O is correct and does not fill the cache"

log_onexit cleanup

# Only one file exists at a time.  The dbuf cache is a global LRU, so a
# second file of this size would evict the first and destroy the comparison.
log_must file_uncached -a -f $FILE -b $BLKSZ -c $COUNT
sync_pool $TESTPOOL
uncached=$(cached_dbufs)

# A read must not populate the cache either.  This has to run while the file
# is still uncached: the hint avoids establishing new cached dbufs but does
# not evict dbufs that something else already cached.
log_must file_uncached -r -a -f $FILE -b $BLKSZ -c $COUNT
rd_uncached=$(cached_dbufs)

# The same read without the hint has to leave the data behind.
log_must file_uncached -r -f $FILE -b $BLKSZ -c $COUNT
rd_cached=$(cached_dbufs)

# Likewise for the write, once the hinted file is out of the way.
log_must rm -f $FILE
log_must file_uncached -f $FILE -b $BLKSZ -c $COUNT
sync_pool $TESTPOOL
cached=$(cached_dbufs)

log_note "cached dbufs: write $uncached/$cached read $rd_uncached/$rd_cached"

# Nothing beyond a stray bonus or spill dbuf should be left behind.
log_must [ $uncached -le 2 ]
log_must [ $rd_uncached -le 2 ]

log_must [ $cached -ge 20 ]
log_must [ $rd_cached -ge 20 ]

log_pass "POSIX_FADV_NOREUSE I/O is correct and does not fill the cache"
