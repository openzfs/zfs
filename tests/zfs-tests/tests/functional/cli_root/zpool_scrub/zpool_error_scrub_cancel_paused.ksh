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
#	An error scrub started after a paused error scrub was canceled is
#	reported as in progress, not as paused.
#
# STRATEGY:
#	1. Create a 10MB file and log errors for it with zinject.
#	2. Start an error scrub (-e), pause it (-p) and cancel it (-s).
#	3. Log the errors again and start a new error scrub (-e).
#	4. Verify that the new error scrub is in progress, not paused, and
#	   that starting another one reports it as in progress.
#

verify_runnable "global"

function cleanup
{
	zpool scrub -s $TESTPOOL 2>/dev/null
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zinject -c all
	rm -f /$TESTPOOL/10m_file
}

log_onexit cleanup

log_assert "An error scrub started after a canceled paused one is not paused."

log_must fio --rw=write --name=job --size=10M --filename=/$TESTPOOL/10m_file

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must zinject -t data -e checksum -f 100 -am /$TESTPOOL/10m_file

dd if=/$TESTPOOL/10m_file bs=1M count=1 || true
log_must sync_pool $TESTPOOL

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub -e $TESTPOOL
log_must is_pool_error_scrubbing $TESTPOOL true
log_must zpool scrub -p $TESTPOOL
log_must is_pool_error_scrub_paused $TESTPOOL true
log_must zpool scrub -s $TESTPOOL
log_must is_pool_error_scrub_stopped $TESTPOOL true

#
# The cancel rotated the error log, and "zpool scrub -e" starts nothing
# while it is empty, so log the errors again first.
#
dd if=/$TESTPOOL/10m_file bs=1M count=1 || true
log_must sync_pool $TESTPOOL
log_must zpool scrub -e $TESTPOOL

log_mustnot is_pool_error_scrub_paused $TESTPOOL true
log_must is_pool_error_scrubbing $TESTPOOL true

errmsg=$(zpool scrub -e $TESTPOOL 2>&1)
log_note "zpool scrub -e: $errmsg"
[[ $errmsg == *"currently error scrubbing"* ]] || \
    log_fail "Running error scrub not reported as in progress"

log_must zpool scrub -s $TESTPOOL
log_must is_pool_error_scrub_stopped $TESTPOOL true

log_pass "An error scrub started after a canceled paused one is not paused."
