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
#	A refault must not overwrite a concurrent O_APPEND write.
#
# STRATEGY:
#	1. Pause a large append on a userfaultfd-backed source page.
#	2. Append a marker from a second writer while the first is paused.
#	3. Verify the marker and both writes are preserved.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Linux O_APPEND refault regression test"
fi

dataset=$TESTPOOL/mmap_write_append_race
mnt=$TESTDIR/mmap_write_append_race
dst=$mnt/data

function cleanup
{
	if datasetexists $dataset; then
		log_must zfs destroy -f $dataset
	fi
}

log_assert "A refault preserves a concurrent append"
log_onexit cleanup

log_must zfs create -o recordsize=16M -o mountpoint=$mnt $dataset

if mmap_write_append_race $dst; then
	log_pass "A refault preserves a concurrent append"
else
	status=$?
	if (( status == 77 )); then
		log_unsupported "userfaultfd is unavailable"
	fi
	log_fail "A refault lost a concurrent append (status $status)"
fi
