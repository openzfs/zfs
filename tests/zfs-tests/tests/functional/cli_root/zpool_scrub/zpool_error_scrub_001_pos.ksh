#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2019 by Delphix. All rights reserved.
# Use is subject to license terms.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify scrub -e, scrub -e -p, and scrub -e -s show the right status.
#
# STRATEGY:
#	1. Create a pool and create a 10MB file in it.
#	2. Start a error scrub (-e) and verify it's doing a scrub.
#	3. Pause error scrub (-e, -p) and verify it's paused.
#	4. Try to pause a paused error scrub (-e, -p) and make sure that fails.
#	5. Resume the paused error scrub and verify again it's doing a scrub.
#	6. Verify zpool scrub -s -e succeed when the system is error scrubbing.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 zfs_scan_suspend_progress 0
	log_must zinject -c all
	rm -f $TESTDIR/10m_file
}

log_onexit cleanup

log_assert "Verify scrub -e, -e -p, and -e -s show the right status."

log_must mkfile 10m $TESTDIR/10m_file

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must zinject -t data -e checksum -f 100 $TESTDIR/10m_file

# create some error blocks
dd if=$TESTDIR/10m_file bs=1M count=1 || true

# sync error blocks to disk
log_must sync_pool $TESTPOOL

log_must set_tunable32 zfs_scan_suspend_progress 1
log_must zpool scrub -e $TESTPOOL
log_must is_pool_error_scrubbing $TESTPOOL true
log_must zpool scrub -e -p $TESTPOOL
log_must is_pool_error_scrub_paused $TESTPOOL true
log_mustnot zpool scrub -e -p $TESTPOOL
log_must is_pool_error_scrub_paused $TESTPOOL true
log_must zpool scrub -e $TESTPOOL
log_must is_pool_error_scrubbing $TESTPOOL true
log_must zpool scrub -e -s $TESTPOOL
log_must is_pool_error_scrub_stopped $TESTPOOL true

log_pass "Verified scrub -e, -s -e, and -p -e show expected status."
