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
