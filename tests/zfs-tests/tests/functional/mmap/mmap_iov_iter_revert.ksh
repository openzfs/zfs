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
# Copyright (c) 2026 by George Melikov.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A failed full-block write restores the Linux iov_iter before retrying.
#
# STRATEGY:
#	1. Keep a two-page source buffer under repeated MADV_DONTNEED pressure.
#	2. pwrite full records into a pre-sized file to exercise fill rollback.
#	3. Verify pwrite does not fail with EFAULT.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Linux iov_iter regression test"
fi

pagesize=$(getconf PAGESIZE)
recordsize=$((pagesize * 2))
recordsize_k=$((recordsize / 1024))
dataset=$TESTPOOL/mmap_write_source_race
mnt=$TESTDIR/mmap_write_source_race
dst=$mnt/data

function cleanup
{
	log_must zfs destroy -f $dataset
}

log_assert "A failed full-block write restores its Linux iov_iter"
log_onexit cleanup

log_must zfs create -o recordsize=${recordsize_k}K -o mountpoint=$mnt $dataset
log_must mmap_write_source_race $dst 512

log_pass "A failed full-block write restores its Linux iov_iter"
