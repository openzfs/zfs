#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version a.0.
# You may only use this file in accordance with the terms of version
# a.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Portions Copyright 2020 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that send with invalid options will fail gracefully.
#
# Strategy:
# 1. Perform zfs send on the cli with the order of the snapshots reversed
# 2. Perform zfs send using libzfs with the order of the snapshots reversed
#

verify_runnable "both"

log_assert "Verify that send with invalid options will fail gracefully."

function cleanup
{
	datasetexists $testfs && destroy_dataset $testfs -r
}
log_onexit cleanup

testfs=$POOL/fs

log_must zfs create $testfs
log_must zfs snap $testfs@snap0
log_must zfs snap $testfs@snap1

# Test bad send with the CLI
log_mustnot eval "zfs send -i $testfs@snap1 $testfs@snap0 >/dev/null"

# Test bad send with libzfs/libzfs_core
log_must badsend $testfs@snap0 $testfs@snap1

log_pass "Send with invalid options fails gracefully."
