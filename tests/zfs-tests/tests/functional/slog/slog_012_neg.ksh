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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Pool can survive when one of mirror log device get corrupted
#
# STRATEGY:
#	1. Create pool with mirror slog devices
#	2. Make corrupted on one disk
#	3. Verify the pool is fine
#

verify_runnable "global"

log_assert "Pool can survive when one of mirror log device get corrupted."
log_onexit cleanup
log_must setup

for type in "" "mirror" "raidz" "raidz2"
do
	for spare in "" "spare"
	do
		log_must zpool create $TESTPOOL $type $VDEV $spare $SDEV \
			log mirror $LDEV

		mntpnt=$(get_prop mountpoint $TESTPOOL)
		#
		# Create file in pool to trigger writing in slog devices
		#
		log_must dd if=/dev/urandom of=$mntpnt/testfile.$$ count=100

		ldev=$(random_get $LDEV)
		log_must mkfile $MINVDEVSIZE $ldev
		log_must zpool scrub $TESTPOOL

		log_must display_status $TESTPOOL
		log_must verify_slog_device $TESTPOOL $ldev 'UNAVAIL' 'mirror'

		log_must zpool destroy -f $TESTPOOL
	done
done

log_pass "Pool can survive when one of mirror log device get corrupted."
