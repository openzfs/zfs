#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/trim/trim.cfg
. $STF_SUITE/tests/functional/trim/trim.kshlib

#
# DESCRIPTION:
#	Verify manual 'zpool trim'.
#
# STRATEGY:
#	1. Create a pool on the provided VDEVS to TRIM.
#	2. Create a small file and sync the pool.
#	3. Remove the file and sync the pool.
#	4. Manually TRIM the pool.
#	5. Verify the completion status.

verify_runnable "global"

log_assert "Run 'zpool trim' to TRIM pool"
log_onexit cleanup_trim

log_must truncate -s $VDEV_SIZE $VDEVS
log_must zpool create -o cachefile=none -f $TRIMPOOL raidz $VDEVS

log_must file_write -o create -f "/$TRIMPOOL/$TESTFILE" -b $BLOCKSIZE -c 16 -w
sync_pool $TRIMPOOL
log_must rm "/$TRIMPOOL/$TESTFILE"
sync_pool $TRIMPOOL

do_trim $TRIMPOOL
log_must zpool destroy $TRIMPOOL

log_pass "Manual TRIM successful"
