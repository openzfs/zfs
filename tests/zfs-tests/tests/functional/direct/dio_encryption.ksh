#!/bin/ksh -p
#
# DDL HEADER START
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

check_dio_write_chksum_verify_failures $TESTPOOL1 "stripe" 0

log_pass "Verified encryption works using Direct I/O"
