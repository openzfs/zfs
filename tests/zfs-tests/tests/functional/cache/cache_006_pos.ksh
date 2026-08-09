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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

#
# DESCRIPTION:
#	Exporting and importing pool with cache devices passes.
#
# STRATEGY:
#	1. Create pool with cache devices.
#	2. Export and import the pool
#	3. Display pool status
#	4. Destroy and import the pool again
#	5. Display pool status
#	6. Destroy and loop to create pool with different configuration.
#

verify_runnable "global"
verify_disk_count "$LDEV2"

log_assert "Exporting and importing pool with cache devices passes."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	log_must zpool create $TESTPOOL $type $VDEV \
		cache $LDEV $LDEV2
	ldev=$(random_get $LDEV $LDEV2)
	log_must verify_cache_device \
		$TESTPOOL $ldev 'ONLINE'

	#
	# Nomal export/import operating
	#
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDIR $TESTPOOL
	log_must display_status $TESTPOOL
	ldev=$(random_get $LDEV $LDEV2)
	log_must verify_cache_device \
		$TESTPOOL $ldev 'ONLINE'

	#
	# Destroy the pool and import again
	#
	log_must zpool destroy $TESTPOOL
	log_must zpool import -Df -d $VDIR $TESTPOOL
	log_must display_status $TESTPOOL
	ldev=$(random_get $LDEV $LDEV2)
	log_must verify_cache_device \
		$TESTPOOL $ldev 'ONLINE'

	log_must zpool destroy -f $TESTPOOL
done

log_pass "Exporting and importing pool with cache devices passes."
