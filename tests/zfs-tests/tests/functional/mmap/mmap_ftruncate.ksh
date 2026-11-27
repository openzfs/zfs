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
# The mmap_ftruncate program dirties every page of a mapped file, starts
# async writeback of all of them, then calls ftruncate() to collapse the
# file, which must wait for any page still under writeback. None of these
# is a syncing operation.
#
# This driver sets a larger zfs_txg_timeout. Test failure occurs when
# ftruncate() blocks waiting for the writeback until the txg timeout is
# reached and the changes are forcibly written out. Success means the DMU
# has accepted the changes and cleared the page writeback flags.
#
# Only the ftruncate() call is timed (mmap_ftruncate prints it). Dirtying
# the pages and writing them back first is throughput-bound: on a slow CI
# VM it took longer than the limit on its own, with ftruncate() still
# near-instant.
#

TIMEOUT=180
TESTFILE=/$TESTPOOL/truncfile
TESTSIZE=$((256*1024*1024)) # 256M

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

# zfs_putpage() counts the bytes it writes back in the dataset's nwritten
typeset nwritten0=$(kstat_dataset $TESTPOOL nwritten)

# run mmap_ftruncate; it prints how long ftruncate() took, in milliseconds
typeset ms
typeset -i start=$(date +%s)
ms=$(mmap_ftruncate $TESTFILE $TESTSIZE) || \
    log_fail "mmap_ftruncate $TESTFILE $TESTSIZE failed"
typeset -i end=$(date +%s)
typeset nwritten=$(($(kstat_dataset $TESTPOOL nwritten) - nwritten0))
log_note "ftruncate() took $ms ms; whole run $((end - start)) s;" \
    "$nwritten bytes written back"

# the async writeback must have pushed the pages to the DMU (zfs_putpage()
# counts them), or the ftruncate() timing says nothing
log_must test $nwritten -ge $TESTSIZE

# when this test passes, the ftruncate() call should be near-instant. when
# it fails, it's only the txg sync that allows ftruncate() to complete, and
# it takes close to the timeout, so require that it completes within half
# the transaction time. for any timeout higher than ~30s that should be a
# very bright line down the middle.
log_must test $ms -lt $((TIMEOUT * 1000 / 2))

log_pass $claim
