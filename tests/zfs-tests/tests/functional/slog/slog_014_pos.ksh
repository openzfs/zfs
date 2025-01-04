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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	log device can survive when one of pool device get corrupted
#
# STRATEGY:
#	1. Create pool with slog devices
#	2. Corrupt the data on one disk.
#	3. Verify the log is fine
#

verify_runnable "global"

log_assert "log device can survive when one of the pool device get corrupted."
log_must setup

for type in "mirror" "raidz" "raidz2"; do
	for spare in "" "spare"; do
		log_must zpool create $TESTPOOL $type $VDEV $spare $SDEV \
			log $LDEV

                # Create a file to be corrupted
                dd if=/dev/urandom of=/$TESTPOOL/filler bs=1024k count=50

                #
                # Ensure the file has been synced out before attempting to
                # corrupt its contents.
                #
                sync_all_pools

		#
		# Corrupt a pool device to make the pool DEGRADED
		# The oseek value below is to skip past the vdev label.
		#
		if is_linux || is_freebsd; then
			log_must dd if=/dev/urandom of=$VDIR/a bs=1024k \
			   seek=4 conv=notrunc count=50
		else
			log_must dd if=/dev/urandom of=$VDIR/a bs=1024k \
			   oseek=4 conv=notrunc count=50
		fi
		log_must zpool scrub $TESTPOOL
		log_must display_status $TESTPOOL
		log_must zpool offline $TESTPOOL $VDIR/a
		log_must wait_for_degraded $TESTPOOL

		log_mustnot eval "zpool status -v $TESTPOOL | grep logs | grep -q \"DEGRADED\""

		log_must zpool online $TESTPOOL $VDIR/a
		log_must zpool destroy -f $TESTPOOL
	done
done

log_pass "log device can survive when one of the pool device get corrupted."
