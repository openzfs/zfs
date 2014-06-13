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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
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

for type in "mirror" "raidz" "raidz2"
do
	for spare in "" "spare"
	do
		log_must $ZPOOL create $TESTPOOL $type $VDEV $spare $SDEV \
			log $LDEV

		# Corrupt a pool device to make the pool DEGRADED
		$DD if=/dev/urandom of=/$TESTPOOL/filler bs=1024k count=50
		# The oseek value below is to skip past the vdev label.
		log_must $DD if=/dev/urandom of=$VDIR/a bs=1024k oseek=4 \
		    conv=notrunc count=50
		log_must $ZPOOL scrub $TESTPOOL
		log_must display_status $TESTPOOL
		log_must $ZPOOL status $TESTPOOL 2>&1 >/dev/null

		$ZPOOL status -v $TESTPOOL | \
			$GREP "state: DEGRADED" 2>&1 >/dev/null
		if (( $? != 0 )); then
			log_fail "pool $TESTPOOL status should be DEGRADED"
		fi

		$ZPOOL status -v $TESTPOOL | $GREP logs | \
			$GREP "DEGRADED" 2>&1 >/dev/null
		if (( $? == 0 )); then
			log_fail "log device should display correct status"
		fi

		log_must $ZPOOL destroy -f $TESTPOOL
	done
done

log_pass "log device can survive when one of the pool device get corrupted."
