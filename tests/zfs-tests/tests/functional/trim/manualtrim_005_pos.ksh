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
#	Verify TRIM and scrub run concurrently.
#
# STRATEGY:
#	1. Create a pool on the provided VDEVS to TRIM.
#       2. Create a small file and sync the pool.
#       3. Remove the file and sync the pool.
#	4. Manually TRIM the pool.
#	5. Manually scrub the pool.
#	6. Verify TRIM and scrub both are reported by 'zpool status'.

verify_runnable "global"

log_assert "Verify TRIM and scrub run concurrently"
log_onexit cleanup_trim

log_must truncate -s $VDEV_SIZE $VDEVS
log_must zpool create -o cachefile=none -f $TRIMPOOL raidz $VDEVS

log_must file_write -o create -f "/$TRIMPOOL/$TESTFILE" -b $BLOCKSIZE -c 1024 -w
sync_pool $TRIMPOOL
log_must rm "/$TRIMPOOL/$TESTFILE"
sync_pool $TRIMPOOL

log_must zpool trim -r 1M $TRIMPOOL
log_must zpool scrub $TRIMPOOL

rate=$(zpool status $TRIMPOOL | tr '()' ' ' | awk '/trim:/ {print $11}')
if [[ "$rate" = "1M/s" ]]; then
	log_note "Pool TRIMming at expected $rate rate"
else
	log_fail "Pool is not TRIMming"
fi

scrub=$(zpool status $TRIMPOOL | awk '/scan:/ { print $2,$3,$4 }')
if [[ "$scrub" = "scrub in progress" ]] || \
    [[ "$scrub" = "scrub repaired 0B" ]]; then
	log_note "Pool scrubbing as expected"
else
	log_fail "Pool is not scrubbing: $scrub"
fi

log_must zpool destroy $TRIMPOOL

log_pass "TRIM and scrub were able to run concurrently"
