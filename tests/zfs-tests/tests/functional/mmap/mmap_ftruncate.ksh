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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# This verifies that async writeback of dirty mmap()'d pages completes quickly.
# ftruncate() is an operation that will trigger async writeback, but is not
# itself a syncing operation, making it a useful proxy for any way the kernel
# might trigger async writeback.
#
# The guts of this test is in the mmap_ftruncate program. This driver sets a
# larger zfs_txg_timeout. Test failure occurs ftruncate() blocks waiting for
# the writeback until the txg timeout is reached and the changes are forcibly
# written out. Success means the DMU has accepted the changes and cleared the
# page dirty flags.
#

TIMEOUT=180
TESTFILE=/$TESTPOOL/truncfile
TESTSIZE=$((2*1024*1024*1024)) # 2G

verify_runnable "global"

typeset claim="async writeback of dirty mmap()'d pages completes quickly"

log_assert $claim

log_must save_tunable TXG_TIMEOUT

function cleanup
{
	log_must restore_tunable TXG_TIMEOUT
	rm -f $TESTFILE
}
log_onexit cleanup

log_must set_tunable32 TXG_TIMEOUT $TIMEOUT
log_must zpool sync -f

# run mmap_ftruncate and record the run time
typeset -i start=$(date +%s)
log_must mmap_ftruncate $TESTFILE $TESTSIZE
typeset -i end=$(date +%s)
typeset -i delta=$((end - start))

# in practice, mmap_ftruncate needs a few seconds to dirty all the pages, and
# when this test passes, the ftruncate() call itself should be near-instant.
# when it fails, then its only the txg sync that allows ftruncate() to
# complete, in that case, the run time will be extremely close to the timeout,
# so to avoid any confusion at the edges, we require that it complets within
# half the transaction time.  for any timeout higher than ~30s that should be a
# very bright line down the middle.
log_must test $delta -lt $((TIMEOUT / 2))

log_pass $claim
