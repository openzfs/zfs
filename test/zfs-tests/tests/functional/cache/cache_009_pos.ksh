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

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

#
# DESCRIPTION:
#	Offline and online a cache device succeed.
#
# STRATEGY:
#	1. Create pool with mirror cache devices.
#	2. Offine and online a cache device
#	3. Display pool status
#	4. Destroy and loop to create pool with different configuration.
#

verify_runnable "global"
verify_disk_count "$LDEV2"

log_assert "Offline and online a cache device succeed."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	log_must $ZPOOL create $TESTPOOL $type $VDEV \
		cache $LDEV $LDEV2

	ldev=$(random_get $LDEV $LDEV2)
	log_must $ZPOOL offline $TESTPOOL $ldev
	log_must display_status $TESTPOOL
	log_must verify_cache_device $TESTPOOL $ldev 'OFFLINE' ''

	log_must $ZPOOL online $TESTPOOL $ldev
	log_must display_status $TESTPOOL
	log_must verify_cache_device $TESTPOOL $ldev 'ONLINE' ''

	log_must $ZPOOL destroy -f $TESTPOOL
done

log_pass "Offline and online a cache device succeed."
