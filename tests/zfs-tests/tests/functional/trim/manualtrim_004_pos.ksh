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
#	Verify 'zpool online|offline|replace' while TRIMming.
#
# STRATEGY:
#	1. Create a pool on the provided VDEVS to TRIM.
#       2. Create a small file and sync the pool.
#       3. Remove the file and sync the pool.
#	4. Manually TRIM the pool.
#	5. Verify 'zpool online|offline|replace' interrupt the TRIM.
#	6. Verify the manual TRIM completes successfully.

verify_runnable "global"

log_assert "Verify 'zpool online|offline|replace' while TRIMming"
log_onexit cleanup_trim

# XXX - Disabled for automated testing only
log_unsupported "Skipping until issue is resolved"

log_must truncate -s $VDEV_SIZE $VDEVS
log_must zpool create -o cachefile=none -f $TRIMPOOL raidz $VDEVS

log_must file_write -o create -f "/$TRIMPOOL/$TESTFILE" -b $BLOCKSIZE -c 1024 -w
sync_pool $TRIMPOOL
log_must rm "/$TRIMPOOL/$TESTFILE"
sync_pool $TRIMPOOL

# Verify 'zpool offline' and 'zpool online'.
for vdev in $VDEVS; do
	# Approximately 64M of TRIMable blocks set 1MB/s TRIM rate.
	log_must zpool trim -r 1M $TRIMPOOL

	# Offline a vdev manual TRIM must continue.
	log_must zpool offline $TRIMPOOL $vdev
	typeset status=$(zpool status $pool | awk '/trim:/ {print $2}')
	if [[ "$status" != "interrupted" ]]; then
		log_note "Manual TRIM is running as expected"
	else
	        log_fail "Manual TRIM was unexpectedly interrupted"
	fi

	# Online a vdev resilver stops manual TRIM.
	log_must zpool online $TRIMPOOL $vdev
	typeset status=$(zpool status $pool | awk '/trim:/ {print $2}')
	if [[ "$status" = "interrupted" ]]; then
		log_note "Manual TRIM was interrupted as expected by resilver"
	else
	        log_fail "Manual TRIM was not interrupted"
	fi

	check_pool $TRIMPOOL
done

# Verify 'zpool replace' by replacing each drive.
log_must truncate -s $VDEV_SIZE $VDEV_DIR/spare
for vdev in $VDEVS; do
	# Approximately 64M of TRIMable blocks set 1MB/s TRIM rate.
	log_must zpool trim -r 1M $TRIMPOOL

	log_must zpool replace $TRIMPOOL $vdev $VDEV_DIR/spare
	typeset status=$(zpool status $pool | awk '/trim:/ {print $2}')
	if [[ "$status" = "interrupted" ]]; then
		log_note "Manual TRIM was interrupted as expected by resilver"
	else
	        log_fail "Manual TRIM was not interrupted"
	fi

	check_pool $TRIMPOOL
	log_must zpool replace $TRIMPOOL $VDEV_DIR/spare $vdev
	check_pool $TRIMPOOL
done
log_must rm $VDEV_DIR/spare

# Allow TRIM to be resumed at full rate and verify completion.
do_trim $TRIMPOOL
log_must zpool destroy $TRIMPOOL

log_pass "Manual TRIM interrupted by 'zpool online|offline|replace' commands"
