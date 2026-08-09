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
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify encryption works using Direct I/O.
#
# STRATEGY:
#	1. Create multidisk pool.
#	2. Start some mixed readwrite Direct I/O.
#	3. Verify the results are as expected using buffered I/O.
#

verify_runnable "global"

log_assert "Verify encryption works using Direct I/O."

log_onexit dio_cleanup

log_must truncate -s $MINVDEVSIZE $DIO_VDEVS

create_pool $TESTPOOL1 $DIO_VDEVS
log_must eval "echo 'password' | zfs create -o encryption=on \
    -o keyformat=passphrase -o keylocation=prompt -o compression=off \
    $TESTPOOL1/$TESTFS1"

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)

for bs in "4k" "128k" "1m"; do
	for op in "rw" "randrw" "write"; do
		dio_and_verify $op $DIO_FILESIZE $bs $mntpnt "sync"
	done
done

log_pass "Verified encryption works using Direct I/O"
