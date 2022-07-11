#!/bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Replacing a log device passes.
#
# STRATEGY:
#	1. Create pool with log devices.
#	2. Replacing one log device
#	3. Display pool status
#	4. Destroy and loop to create pool with different configuration.
#

verify_runnable "global"

log_assert "Replacing a log device passes."
log_onexit cleanup
log_must setup

for type in "" "mirror" "raidz" "raidz2"
do
	for spare in "" "spare"
	do
		for logtype in "" "mirror"
		do
			log_must zpool create $TESTPOOL $type $VDEV \
				$spare $SDEV log $logtype $LDEV
			sdev=$(random_get $LDEV)
			tdev=$(random_get $LDEV2)
			log_must zpool replace $TESTPOOL $sdev $tdev
			log_must display_status $TESTPOOL
			# sleep 15 to make sure replacement completely.
			log_must sleep 15
			log_must verify_slog_device \
				$TESTPOOL $tdev 'ONLINE' $logtype

			log_must zpool destroy -f $TESTPOOL
		done
	done
done

log_pass "Replacing a log device passes."
