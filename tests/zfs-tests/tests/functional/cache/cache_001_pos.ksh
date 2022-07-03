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
#	Creating a pool with a cache device succeeds.
#
# STRATEGY:
#	1. Create pool with separated cache devices.
#	2. Display pool status
#	3. Destroy and loop to create pool with different configuration.
#

verify_runnable "global"

log_assert "Creating a pool with a cache device succeeds."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	log_must zpool create $TESTPOOL $type $VDEV \
		cache $LDEV
	log_must display_status $TESTPOOL

	ldev=$(random_get $LDEV)
	log_must verify_cache_device $TESTPOOL $ldev 'ONLINE'

	log_must zpool remove $TESTPOOL $ldev
	log_must check_vdev_state $TESTPOOL $ldev ""

	log_must zpool destroy -f $TESTPOOL
done

log_pass "Creating a pool with a cache device succeeds."
