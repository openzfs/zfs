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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cachefile/cachefile.cfg
. $STF_SUITE/tests/functional/cachefile/cachefile.kshlib

#
# DESCRIPTION:
#	A cache file in a directory that was just created is written and
#	holds the pool.
#
# STRATEGY:
#	1. Create a directory, then a pool with cachefile set to a file
#	   in it
#	2. Verify that the cache file holds the pool, and that no
#	   config_cache_write event was posted
#	3. Create another directory, and set cachefile to a file in it
#	4. Verify the same for that cache file
#
# NOTES:
#	On FreeBSD, UFS with soft updates fails the fsync of a file with
#	ERELOOKUP, which asks the caller to retry, when the name of the
#	file is in a new directory block that is not written yet.  Without
#	the retry, ZFS removed the cache file it had just written and
#	posted the event.  The test only detects that when $TEST_BASE_DIR
#	is on such a file system, as /var/tmp is in FreeBSD CI.
#

verify_runnable "global"

typeset DIR1=$TEST_BASE_DIR/cachefile_005_pos.1.$$
typeset DIR2=$TEST_BASE_DIR/cachefile_005_pos.2.$$

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $DIR1 $DIR2
}

function check_cachefile
{
	typeset cachefile=$1

	log_must test -s $cachefile
	log_must pool_in_cache $TESTPOOL $cachefile
	if zpool events | grep -q config_cache_write; then
		log_fail "writing $cachefile posted a config_cache_write event"
	fi
}

log_assert "A cache file in a new directory is written and holds the pool"
log_onexit cleanup

log_must zpool events -c

log_must mkdir $DIR1
log_must zpool create -o cachefile=$DIR1/zpool.cache $TESTPOOL $DISKS
check_cachefile $DIR1/zpool.cache

log_must mkdir $DIR2
log_must zpool set cachefile=$DIR2/zpool.cache $TESTPOOL
check_cachefile $DIR2/zpool.cache

log_pass "A cache file in a new directory is written and holds the pool"
