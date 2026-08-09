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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify different recordsizes are supported for Direct I/O.
#
# STRATEGY:
#	1. Create a pool from each vdev type with varying recordsizes.
#	2. Start sequential Direct I/O and verify with buffered I/O.
#	3. Start mixed Direct I/O and verify with buffered I/O.
#

verify_runnable "global"

log_assert "Verify different recordsizes are supported for Direct I/O."

log_onexit dio_cleanup

log_must truncate -s $MINVDEVSIZE $DIO_VDEVS

for type in "" "mirror" "raidz" "draid"; do
	for recsize in "1024" "4096" "128k"; do
		create_pool $TESTPOOL1 $type $DIO_VDEVS
		log_must eval "zfs create \
		    -o recordsize=$recsize -o compression=off \
		    $TESTPOOL1/$TESTFS1"

		mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)

		for bs in "4k" "128k"; do
			for op in "rw" "randrw" "write"; do
				dio_and_verify $op $DIO_FILESIZE $bs $mntpnt "sync"
			done
		done

		destroy_pool $TESTPOOL1
	done
done

log_pass "Verified different recordsizes are supported for Direct I/O."
