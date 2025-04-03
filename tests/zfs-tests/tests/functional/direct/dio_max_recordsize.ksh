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
# Copyright (c) 2022 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify max recordsizes are supported for Direct I/O.
#
# STRATEGY:
#	1. Create a pool from each vdev type with varying recordsizes.
#	2. Start sequential Direct I/O and verify with buffered I/O.
#

verify_runnable "global"

log_assert "Verify max recordsizes are supported for Direct I/O."

log_onexit dio_cleanup

log_must truncate -s $MINVDEVSIZE $DIO_VDEVS

for type in "" "mirror" "raidz" "draid"; do;
	for recsize in "2097152" "8388608" "16777216"; do
		create_pool $TESTPOOL1 $type $DIO_VDEVS
		log_must eval "zfs create \
		    -o recordsize=$recsize -o compression=off \
		    $TESTPOOL1/$TESTFS1"

		mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)

		verify_dio_write_count $TESTPOOL1 $recsize $((4 * recsize)) \
		    $mntpnt

		destroy_pool $TESTPOOL1
	done
done

log_pass "Verified max recordsizes are supported for Direct I/O."
