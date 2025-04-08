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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	A raidz/raidz2 log can not be added to existed pool.
#
# STRATEGY:
#	1. Create pool with or without log.
#	2. Add a raidz/raidz2 log to this pool.
#	3. Verify failed to add.
#

verify_runnable "global"

log_assert "A raidz/raidz2 log can not be added to existed pool."
log_onexit cleanup
log_must setup

for type in "" "mirror" "raidz" "raidz2"
do
	for spare in "" "spare"
	do
		for logtype in "raidz" "raidz1" "raidz2"
		do
			log=$(random_get_with_non "log")
			log_must zpool create $TESTPOOL $type $VDEV \
				$spare $SDEV $log $LDEV

			log_mustnot zpool add $TESTPOOL log $logtype $LDEV2
			ldev=$(random_get $LDEV2)
			log_mustnot verify_slog_device \
				$TESTPOOL $ldev 'ONLINE' $logtype

			log_must zpool destroy $TESTPOOL
		done
	done
done

log_pass "A raidz/raidz2 log can not be added to existed pool."
