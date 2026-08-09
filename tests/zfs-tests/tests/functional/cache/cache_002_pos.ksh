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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

#
# DESCRIPTION:
#	Adding a cache device to normal pool works.
#
# STRATEGY:
#	1. Create pool
#	2. Add cache devices with different configuration
#	3. Display pool status
#	4. Destroy and loop to create pool with different configuration.
#

verify_runnable "global"

log_assert "Adding a cache device to normal pool works."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	log_must zpool create $TESTPOOL $type $VDEV
	log_must zpool add $TESTPOOL cache $LDEV
	log_must display_status $TESTPOOL
	typeset ldev=$(random_get $LDEV)
	log_must verify_cache_device $TESTPOOL $ldev 'ONLINE'

	log_must zpool remove $TESTPOOL $ldev
	log_must check_vdev_state $TESTPOOL $ldev ""

	log_must zpool destroy -f $TESTPOOL
done

log_pass "Adding a cache device to normal pool works."
