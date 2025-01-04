#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb will accurately count the feature refcount for pools with and without
# features enabled.
#
# Strategy:
# 1. Create a pool, and collect zdb output for the pool.
# 2. Verify there are no 'feature refcount mismatch' messages.
# 3. Repeat for a pool with features disabled.
#

log_assert "Verify zdb accurately counts feature refcounts."
log_onexit cleanup

typeset errstr="feature refcount mismatch"
typeset tmpfile="$TEST_BASE_DIR/zdb-feature-mismatch"
function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	grep "$errstr" $tmpfile
	rm -f $tmpfile
}

for opt in '' -d; do
	log_must zpool create -f $opt $TESTPOOL ${DISKS%% *}
	log_must eval "zdb $TESTPOOL >$tmpfile"
	grep -q "$errstr" $tmpfile && \
	    log_fail "Found feature refcount mismatches in zdb output."
	destroy_pool $TESTPOOL
done

log_pass "zdb accurately counts feature refcounts."
