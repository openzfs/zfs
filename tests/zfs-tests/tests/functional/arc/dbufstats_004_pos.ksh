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
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

#
# DESCRIPTION:
# Indirect blocks have a dbuf cache of their own, and churning level 0 blocks
# through the dbuf cache must not evict them.
#
# STRATEGY:
# 1. Make the level 0 dbuf cache small, and leave the indirect block cache at
#    its default (generous) size.
# 2. Create a set of files whose level 0 blocks are far larger than the level
#    0 cache, then read all of them once so that their indirect blocks are
#    cached.
# 3. Read all of them repeatedly, forcing level 0 evictions.
# 4. Verify that level 0 blocks were evicted while indirect blocks were not,
#    and that the indirect block cache is reported consistently.
# 5. Repeat with an object deep enough to have a level 2 indirect block, to
#    confirm that a deeper object is handled the same way.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

BLKSZ=131072
FILEBLOCKS=16
TESTFILES=8

# Small enough that the level 0 blocks of the test files cannot all fit.
LEVEL0_CACHE=524288

DATASET=$TESTPOOL/dbufind
TESTDIR=/$DATASET

# A second, deeper object.  With the default 128K indirect blocks a level 2
# indirect block appears as soon as an object needs more level 1 blocks than
# the dnode has block pointers for, which is only a handful, so a small
# recordsize keeps both the file and the test small.  The block count is kept
# well clear of that threshold rather than just over it.
DEEPFS=$TESTPOOL/dbufind_deep
DEEPDIR=/$DEEPFS
DEEPRECSIZE=512
DEEPFILEBLOCKS=10000

orig_max=$(get_tunable DBUF_CACHE_MAX_BYTES)

function cleanup
{
	[[ -e $TESTDIR ]] && log_must rm -Rf $TESTDIR/*
	[[ -e $DEEPDIR ]] && log_must rm -Rf $DEEPDIR/*
	destroy_dataset $DATASET
	destroy_dataset $DEEPFS
	log_must set_tunable64 DBUF_CACHE_MAX_BYTES $orig_max
}

function readall
{
	typeset f

	for f in $TESTDIR/file*; do
		log_must dd if=$f of=/dev/null bs=$BLKSZ
	done
}

function cachestats
{
	log_note "$1: level0 $(kstat dbufstats.cache_size_bytes) bytes in " \
	    "$(kstat dbufstats.cache_level_0) dbufs, level1 " \
	    "$(kstat dbufstats.cache_level_1) dbufs, level2 " \
	    "$(kstat dbufstats.cache_level_2) dbufs, indirect " \
	    "$(kstat dbufstats.indirect_cache_size_bytes) bytes in " \
	    "$(kstat dbufstats.indirect_cache_count) dbufs, " \
	    "metadata $(kstat dbufstats.metadata_cache_count) dbufs, " \
	    "evicts $(kstat dbufstats.cache_total_evicts) (skips " \
	    "$(kstat dbufstats.cache_evict_skips))"
}

log_assert "indirect blocks are cached separately from level 0 blocks"
log_onexit cleanup

log_must zfs create -o recordsize=128k -o primarycache=all \
    -o compression=off $DATASET
log_must set_tunable64 DBUF_CACHE_MAX_BYTES $LEVEL0_CACHE

log_note "recordsize $(zfs get -H -o value recordsize $DATASET), " \
    "primarycache $(zfs get -H -o value primarycache $DATASET)"
log_note "dbuf cache target $(kstat dbufstats.cache_target_bytes) bytes, " \
    "of which indirect $(kstat dbufstats.indirect_cache_target_bytes) bytes"

typeset -i i=0
while [ $i -lt $TESTFILES ]; do
	log_must file_write -o create -f $TESTDIR/file$i \
	    -b $BLKSZ -c $FILEBLOCKS
	((i = i + 1))
done
sync_pool $TESTPOOL

# Read everything once so that every indirect block is in the dbuf cache.
readall
cachestats "after warmup"

typeset -i ind_evicts1=$(kstat dbufstats.indirect_cache_total_evicts)
typeset -i l0_evicts1=$(kstat dbufstats.cache_total_evicts)

# Now churn the level 0 blocks.
i=0
while [ $i -lt 3 ]; do
	readall
	((i = i + 1))
done
cachestats "after churn"

# Give the eviction thread a chance to catch up.
sleep 3
cachestats "after settle"

typeset -i ind_evicts2=$(kstat dbufstats.indirect_cache_total_evicts)
typeset -i l0_evicts2=$(kstat dbufstats.cache_total_evicts)

log_note "cache evictions: $l0_evicts1 -> $l0_evicts2"
log_note "indirect cache evictions: $ind_evicts1 -> $ind_evicts2"

# Reading the files repeatedly must push level 0 blocks out of the dbuf cache.
log_must [ $l0_evicts2 -gt $l0_evicts1 ]

# ... but it must not push the indirect blocks out, because they have a cache
# of their own that the level 0 blocks cannot overflow.
log_must [ $ind_evicts2 -eq $ind_evicts1 ]

# The indirect blocks really are cached, and the indirect block cache is
# bounded by a fraction of the ARC target, which is part of the total dbuf
# cache target.
log_must [ $(kstat dbufstats.cache_level_1) -gt 0 ]
log_must [ $(kstat dbufstats.indirect_cache_size_bytes) -gt 0 ]
log_must [ $(kstat dbufstats.indirect_cache_target_bytes) -gt 0 ]
log_must [ $(kstat dbufstats.cache_target_bytes) -ge \
    $(kstat dbufstats.indirect_cache_target_bytes) ]

# Indirect blocks of every level share the one cache, so read an object deep
# enough to have a level 2 indirect block as well.
typeset -i l1_before=$(kstat dbufstats.cache_level_1)

log_must zfs create -o recordsize=$DEEPRECSIZE -o primarycache=all \
    -o compression=off $DEEPFS
log_must file_write -o create -f $DEEPDIR/file -b $DEEPRECSIZE \
    -c $DEEPFILEBLOCKS
sync_pool $TESTPOOL
log_must dd if=$DEEPDIR/file of=/dev/null bs=$DEEPRECSIZE

# Check the premise: the object really does have more than one indirect level.
# That is a property of the on-disk block tree, so ask zdb.
objid=$(get_objnum $DEEPDIR/file)
typeset -i lvls=$(zdb -dddd $DEEPFS $objid |
    awk -v o=$objid '$1 == o && $2 ~ /^[0-9]+$/ { print $2; exit }')
log_must [ "$lvls" -ge 3 ]

log_note "deep object: lvl $lvls, level0 $(kstat dbufstats.cache_level_0) " \
    "dbufs, level1 $(kstat dbufstats.cache_level_1) dbufs, level2 " \
    "$(kstat dbufstats.cache_level_2) dbufs"

# Its level 1 blocks are in the indirect block cache.  Those that still have
# cached level 0 children are held by them and so are not cached themselves,
# which is why a margin is allowed for here.
log_must [ $(kstat dbufstats.cache_level_1) -ge $((l1_before + 2)) ]

# The level 2 block is not itself in the cache: a dbuf holds a reference on its
# parent for as long as it exists (see dbuf_create()), so the level 2 block is
# kept alive by the cached level 1 blocks instead.  It moves into the cache
# only once its children have been evicted.

log_pass "indirect blocks are cached separately from level 0 blocks"
