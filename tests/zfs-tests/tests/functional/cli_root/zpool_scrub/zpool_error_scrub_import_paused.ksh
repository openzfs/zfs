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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	A paused error scrub is still reported as paused after the pool is
#	exported and imported, on a pool that has never been scrubbed.
#
# STRATEGY:
#	1. Create a pool on a file vdev, write a 10MB file and log errors
#	   for it with zinject.
#	2. Start an error scrub (-e) and pause it (-p).
#	3. Export and import the pool.
#	4. Verify that the error scrub is reported as paused, not in
#	   progress.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	zinject -c all
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

log_onexit cleanup

log_assert "A paused error scrub is still paused after export and import."

log_must truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f $TESTPOOL2 $TESTDIR/vdev_a
log_must fio --rw=write --name=job --size=10M --filename=/$TESTPOOL2/10m_file

log_must zpool export $TESTPOOL2
log_must zpool import -d $TESTDIR $TESTPOOL2
log_must zinject -t data -e checksum -f 100 -am /$TESTPOOL2/10m_file

dd if=/$TESTPOOL2/10m_file bs=1M count=1 || true
log_must sync_pool $TESTPOOL2
log_must zinject -c all

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub -e $TESTPOOL2
log_must is_pool_error_scrubbing $TESTPOOL2 true
log_must zpool scrub -p $TESTPOOL2
log_must is_pool_error_scrub_paused $TESTPOOL2 true

log_must zpool export $TESTPOOL2
log_must zpool import -d $TESTDIR $TESTPOOL2

log_mustnot is_pool_error_scrubbing $TESTPOOL2 true
log_must is_pool_error_scrub_paused $TESTPOOL2 true

log_pass "A paused error scrub is still paused after export and import."
