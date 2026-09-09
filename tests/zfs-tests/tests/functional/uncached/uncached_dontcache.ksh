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
# Verify that RWF_DONTCACHE I/O returns correct data and leaves none of the
# file's data blocks in the dbuf cache.
#
# STRATEGY:
# 1. Write a file with RWF_DONTCACHE, then read it back the same way, then
#    read it back without the flag, then write it again without the flag.
# 2. After each step count the level 0 dbufs the file left in the dbuf cache.
#    Counting per object rather than watching dbufstats.cache_count keeps the
#    result independent of whatever else on the system is using ZFS.
# 3. The RWF_DONTCACHE steps must leave nothing behind and the plain ones
#    must not, or the checks would pass for the wrong reason.  The reads also
#    verify the pattern that was written.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "RWF_DONTCACHE is Linux-only"
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

log_assert "RWF_DONTCACHE I/O is correct and does not fill the dbuf cache"

log_onexit cleanup

# Only one file exists at a time.  The dbuf cache is a global LRU, so a
# second file of this size would evict the first and destroy the comparison.
file_uncached -d -f $FILE -b $BLKSZ -c $COUNT
ret=$?
if [[ $ret -eq 2 ]]; then
	log_unsupported "RWF_DONTCACHE is not supported by this kernel"
elif [[ $ret -ne 0 ]]; then
	log_fail "file_uncached write failed with $ret"
fi
sync_pool $TESTPOOL
uncached=$(cached_dbufs)

# A read must not populate the cache either.  This has to run while the file
# is still uncached: uncached I/O avoids establishing new cached dbufs, it
# does not evict dbufs that something else already cached.
log_must file_uncached -r -d -f $FILE -b $BLKSZ -c $COUNT
rd_uncached=$(cached_dbufs)

# The same read without the flag has to leave the data behind.
log_must file_uncached -r -f $FILE -b $BLKSZ -c $COUNT
rd_cached=$(cached_dbufs)

# Likewise for the write, once the uncached file is out of the way.
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

log_pass "RWF_DONTCACHE I/O is correct and does not fill the dbuf cache"
