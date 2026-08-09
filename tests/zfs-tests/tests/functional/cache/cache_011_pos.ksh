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
#	Remove cache device from pool with spare device should succeed.
#
# STRATEGY:
#	1. Create pool with cache devices and spare devices
#	2. Remove cache device from the pool
#	3. The upper action should succeed
#

verify_runnable "global"
verify_disk_count "$LDEV2"

function cleanup {
	if datasetexists $TESTPOOL ; then
		log_must zpool destroy -f $TESTPOOL
	fi
}

log_assert "Remove cache device from pool with spare device should succeed"
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	log_must zpool create $TESTPOOL $type $VDEV \
		cache $LDEV spare $LDEV2

	log_must zpool remove $TESTPOOL $LDEV
	log_must display_status $TESTPOOL

	log_must zpool destroy -f $TESTPOOL
done

log_pass "Remove cache device from pool with spare device should succeed"
