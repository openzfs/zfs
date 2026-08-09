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
# Copyright (c) 2025, iXsystems, Inc.
#

# DESCRIPTION:
#	Verify zfs rewrite -P flag correctly preserves logical birth times.
#
# STRATEGY:
#	1. Create a test file and sync it.
#	2. Create a snapshot to capture the original birth time.
#	3. Test default rewrite behavior (updates logical birth time).
#	4. Test -P flag behavior (preserves logical birth time).
#	5. Verify incremental send behavior difference.

. $STF_SUITE/include/libtest.shlib

typeset tmp=$(mktemp)
typeset send_default=$(mktemp)
typeset send_physical=$(mktemp)

function cleanup
{
	rm -rf $tmp $send_default $send_physical $TESTDIR/*
	zfs destroy -R $TESTPOOL/$TESTFS@snap1 2>/dev/null || true
	zfs destroy -R $TESTPOOL/$TESTFS@snap2 2>/dev/null || true
	zfs destroy -R $TESTPOOL/$TESTFS@snap3 2>/dev/null || true
}

log_assert "zfs rewrite -P flag correctly preserves logical birth times"

log_onexit cleanup

log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

# Create test file and initial snapshot
log_must dd if=/dev/urandom of=$TESTDIR/testfile bs=128k count=4
log_must sync_pool $TESTPOOL
typeset orig_hash=$(xxh128digest $TESTDIR/testfile)
log_must zfs snapshot $TESTPOOL/$TESTFS@snap1

# Test default rewrite behavior (updates logical birth time)
log_must zfs rewrite $TESTDIR/testfile
log_must sync_pool $TESTPOOL
typeset default_hash=$(xxh128digest $TESTDIR/testfile)
log_must [ "$orig_hash" = "$default_hash" ]
log_must zfs snapshot $TESTPOOL/$TESTFS@snap2

# Test incremental send size - should be large with updated birth time
log_must eval "zfs send -i @snap1 $TESTPOOL/$TESTFS@snap2 > $send_default"
typeset default_size=$(wc -c < $send_default)
log_note "Default rewrite incremental send size: $default_size bytes"

# Reset the file to original state
log_must zfs rollback -r $TESTPOOL/$TESTFS@snap1

# Test -P flag behavior (preserves logical birth time)
log_must zfs rewrite -P $TESTDIR/testfile
log_must sync_pool $TESTPOOL
typeset physical_hash=$(xxh128digest $TESTDIR/testfile)
log_must [ "$orig_hash" = "$physical_hash" ]
log_must zfs snapshot $TESTPOOL/$TESTFS@snap3

# Test incremental send size - should be minimal with preserved birth time
log_must eval "zfs send -i @snap1 $TESTPOOL/$TESTFS@snap3 > $send_physical"
typeset physical_size=$(wc -c < $send_physical)
log_note "Physical rewrite incremental send size: $physical_size bytes"

# Verify that -P flag produces smaller incremental send
if [[ $physical_size -lt $default_size ]]; then
	log_note "SUCCESS: -P flag produces smaller incremental send" \
	    "($physical_size < $default_size)"
else
	log_fail "FAIL: -P flag should produce smaller incremental send" \
	    "($physical_size >= $default_size)"
fi

log_pass
