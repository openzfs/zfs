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
# Copyright (c) 2017 by Tim Chase. All rights reserved.
# Copyright (c) 2017 by Nexenta Systems, Inc. All rights reserved.
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/trim/trim.cfg
. $STF_SUITE/tests/functional/trim/trim.kshlib

#
# DESCRIPTION:
#	Verify 'zpool import|export' interrupts TRIM.
#
# STRATEGY:
#	1. Create a pool on the provided VDEVS to TRIM.
#       2. Create a small file and sync the pool.
#       3. Remove the file and sync the pool.
#	4. Manually TRIM the pool.
#	5. Export then import the TRIMing pool.
#	6. Verify the manual TRIM was interrupted.
#	7. Verify the manual TRIM can be resumed and complete successfully.

verify_runnable "global"

log_assert "Verify 'zpool import|export' during TRIM resumes"
log_onexit cleanup_trim

log_must truncate -s $VDEV_SIZE $VDEVS
log_must zpool create -o cachefile=none -f $TRIMPOOL raidz $VDEVS

log_must file_write -o create -f "/$TRIMPOOL/$TESTFILE" -b $BLOCKSIZE -c 16 -w
sync_pool $TRIMPOOL
log_must rm "/$TRIMPOOL/$TESTFILE"
sync_pool $TRIMPOOL

log_must zpool trim -r 1 $TRIMPOOL
log_must zpool export $TRIMPOOL
log_must zpool import -d $VDEV_DIR $TRIMPOOL

typeset status=$(zpool status $pool | awk '/trim:/ {print $2}')
if [[ "$status" = "interrupted" ]]; then
	log_note "Manual TRIM was interrupted"
else
	log_fail "Manual TRIM was not interrupted, status is $status"
fi

# Allow TRIM to be resumed at full rate and verify completion.
do_trim $TRIMPOOL
log_must zpool destroy $TRIMPOOL

log_pass "Manual TRIM interrupted and resumed after import"
