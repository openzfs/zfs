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
# Verify that blocks written sequentially in parts are not kept in the dbuf
# cache with primarycache=none once the writes have completed them.
#
# STRATEGY:
# 1. With primarycache=none, write a file of whole records in 1k parts, so
#    that every record is first dirtied by a partial write and then
#    redirtied by the following writes in the same txg.
# 2. Sync and count the file's level 0 dbufs left in the dbuf cache; there
#    must be none.
# 3. Repeat with primarycache=all, where the dbufs must stay cached, so the
#    first check can't pass for the wrong reason.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS
FILE=$TESTDIR/$TESTFILE0
RECSIZE=131072
NRECS=8
DBUFS_FILE=$(mktemp -t dbufs.out.XXXXXX)

function cleanup
{
	log_must rm -f $FILE $DBUFS_FILE
	log_must zfs inherit primarycache $FS
	log_must zfs inherit recordsize $FS
	log_must zfs inherit compression $FS
}

function cached_dbufs
{
	typeset objset=$(get_prop objsetid $FS)
	typeset obj=$(get_objnum $FILE)

	kstat dbufs > $DBUFS_FILE
	dbufstat -bxn -i $DBUFS_FILE \
	    -F "pool=$TESTPOOL,objset=$objset,object=$obj,level=0" | wc -l
}

# write_in_parts: write $NRECS whole records to $FILE, 1k at a time.  The
# file must end on a record boundary: a partial last block is meant to stay
# cached for a writer that appends to it.
function write_in_parts
{
	log_must rm -f $FILE
	log_must dd if=/dev/urandom of=$FILE bs=1024 \
	    count=$((NRECS * RECSIZE / 1024))
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
}

log_assert "Blocks written in parts are not cached with primarycache=none"

log_onexit cleanup

log_must zfs set recordsize=$RECSIZE $FS
log_must zfs set compression=off $FS

log_must zfs set primarycache=none $FS
write_in_parts
uncached=$(cached_dbufs)

log_must zfs set primarycache=all $FS
write_in_parts
cached=$(cached_dbufs)

log_note "cached level 0 dbufs: primarycache=none $uncached," \
    "primarycache=all $cached (of $NRECS)"

log_must [ $uncached -eq 0 ]
log_must [ $cached -ge $((NRECS / 2)) ]

log_pass "Blocks written in parts are not cached with primarycache=none"
