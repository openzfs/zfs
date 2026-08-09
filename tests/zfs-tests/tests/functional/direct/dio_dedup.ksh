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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify deduplication works. Deduplication is disabled when issuing
# 	Direct I/O writes.
#
# STRATEGY:
#	1. Enable dedup
#	2. Start sequential Direct I/O and verify with buffered I/O
#	3. Start mixed Direct IO and verify with buffered I/O
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$mntpnt/direct-*"
	log_must zfs set dedup=off $TESTPOOL/$TESTFS
}

log_assert "Verify deduplication works using Direct I/O."

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
dedup_args="--dedupe_percentage=50"

log_must zfs set dedup=on $TESTPOOL/$TESTFS
for op in "rw" "randrw" "write"; do
	dio_and_verify $op $DIO_FILESIZE $DIO_BS $mntpnt "sync" $dedup_args
done

log_pass "Verfied deduplication works using Direct I/O"
