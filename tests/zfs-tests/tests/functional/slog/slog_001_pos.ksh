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
#	Creating a pool with a log device succeeds.
#
# STRATEGY:
#	1. Create pool with separated log devices.
#	2. Display pool status
#	3. Destroy and loop to create pool with different configuration.
#

verify_runnable "global"

log_assert "Creating a pool with a log device succeeds."
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
			log_must display_status $TESTPOOL

			ldev=$(random_get $LDEV)
			log_must verify_slog_device \
				$TESTPOOL $ldev 'ONLINE' $logtype

			log_must zpool destroy -f $TESTPOOL
		done
	done
done

log_pass "Creating a pool with a log device succeeds."
