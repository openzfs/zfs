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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Attaching a log device passes.
#
# STRATEGY:
#	1. Create pool with separated log devices.
#	2. Attaching a log device for existing log device
#	3. Display pool status
#	4. Destroy and loop to create pool with different configuration.
#

verify_runnable "global"

log_assert "Attaching a log device passes."
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

			ldev=$(random_get $LDEV)
			typeset ldev2=$(random_get $LDEV2)
			log_must zpool attach $TESTPOOL $ldev $ldev2
			log_must display_status $TESTPOOL
			log_must verify_slog_device \
				$TESTPOOL $ldev 'ONLINE' 'mirror'
			log_must verify_slog_device \
				$TESTPOOL $ldev2 'ONLINE' 'mirror'

			log_must zpool destroy -f $TESTPOOL
		done
	done
done

log_pass "Attaching a log device passes."
