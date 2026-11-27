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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A failed lagging child that is detached is no longer a failed copy:
#	a waiting zpool sync does not report that the async mirror did not
#	catch up, and zpool status shows no async mirror failure.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Inject write errors on the lagging child, write data, and wait
#	   until the copy fails.  Clear the injection and wait until the
#	   child is resilvered.
#	3. Verify that a waiting zpool sync reports the failed copy.
#	4. Detach the failed child.
#	5. Verify that a waiting zpool sync reports nothing and that zpool
#	   status shows no failed async mirror.
#

verify_runnable "global"

typeset sync_err=$TEST_BASE_DIR/async_mirror_detach_sync.err.$$

function cleanup
{
	zinject -c all > /dev/null 2>&1
	rm -f $sync_err
	cleanup_async_mirror
}

log_onexit cleanup

log_assert "A detached failed lagging child is no longer a failed copy."

async_mirror_create_pool
async_mirror_enable

log_must zinject -d $(get_leaf_guid $ASYNC_MIRROR_LAGGING) -e io \
    -T write $TESTPOOL
async_mirror_write_files file 2
log_must wait_async_mirror_failed
log_must zinject -c all
log_must wait_async_mirror_resilvered

log_must eval "zpool sync $TESTPOOL 2> $sync_err"
log_must grep -q "async mirror did not catch up" $sync_err

log_must zpool detach $TESTPOOL $ASYNC_MIRROR_LAGGING

log_must eval "zpool sync $TESTPOOL 2> $sync_err"
log_mustnot grep -q "async mirror did not catch up" $sync_err
log_mustnot eval "zpool status -x $TESTPOOL | grep -q 'asynchronous mirror'"

log_pass "A detached failed lagging child is no longer a failed copy."
