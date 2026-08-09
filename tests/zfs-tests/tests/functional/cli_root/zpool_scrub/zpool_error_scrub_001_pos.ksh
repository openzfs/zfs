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
# Copyright (c) 2019, Delphix. All rights reserved.
# Copyright (c) 2023, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify scrub -e, -p, and -s show the right status.
#
# STRATEGY:
#	1. Create a pool and create a 10MB file in it.
#	2. Start a error scrub (-e) and verify it's doing a scrub.
#	3. Pause error scrub (-p) and verify it's paused.
#	4. Try to pause a paused error scrub (-p) and make sure that fails.
#	5. Resume the paused error scrub and verify again it's doing a scrub.
#	6. Verify zpool scrub -s succeed when the system is error scrubbing.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zinject -c all
	rm -f /$TESTPOOL/10m_file
}

log_onexit cleanup

log_assert "Verify scrub -e, -p, and -s show the right status."

log_must fio --rw=write --name=job --size=10M --filename=/$TESTPOOL/10m_file

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must zinject -t data -e checksum -f 100 -am /$TESTPOOL/10m_file

# create some error blocks
dd if=/$TESTPOOL/10m_file bs=1M count=1 || true

# sync error blocks to disk
log_must sync_pool $TESTPOOL

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub -e $TESTPOOL
log_must is_pool_error_scrubbing $TESTPOOL true
log_must zpool scrub -p $TESTPOOL
log_must is_pool_error_scrub_paused $TESTPOOL true
log_mustnot zpool scrub -p $TESTPOOL
log_must is_pool_error_scrub_paused $TESTPOOL true
log_must zpool scrub -e $TESTPOOL
log_must is_pool_error_scrubbing $TESTPOOL true
log_must zpool scrub -s $TESTPOOL
log_must is_pool_error_scrub_stopped $TESTPOOL true

log_pass "Verified scrub -e, -p, and -s show expected status."
