#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
