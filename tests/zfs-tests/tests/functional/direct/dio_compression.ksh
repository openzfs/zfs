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
# 	Verify compression works using Direct I/O.
#
# STRATEGY:
#	1. Select a random compression algoritm
#	2. Start sequential Direct I/O and verify with buffered I/O
#	3. Start mixed Direct I/O and verify with buffered I/O
#	4. Repeat from 2 for all compression algoritms
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$mntpnt/direct-*"
	log_must zfs set compression=off $TESTPOOL/$TESTFS
}

log_assert "Verify compression works using Direct I/O."

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
compress_args="--buffer_compress_percentage=50"

for comp in "${compress_prop_vals[@]:1}"; do
	log_must zfs set compression=$comp $TESTPOOL/$TESTFS
	for op in "rw" "randrw" "write"; do
		dio_and_verify $op $DIO_FILESIZE $DIO_BS $mntpnt "sync" $compress_args
	done
done

log_pass "Verfied compression works using Direct I/O"
