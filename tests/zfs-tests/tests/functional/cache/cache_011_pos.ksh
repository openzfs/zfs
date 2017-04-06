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
