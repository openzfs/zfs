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
#	Attaching a cache device fails.
#
# STRATEGY:
#	1. Create pool with separated cache devices.
#	2. Attaching a cache device for existing cache device
#	3. Verify the operation fails
#

verify_runnable "global"
verify_disk_count "$LDEV2"

log_assert "Attaching a cache device fails for an existing cache device."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	log_must zpool create $TESTPOOL $type $VDEV \
		cache $LDEV

	ldev=$(random_get $LDEV)
	typeset ldev2=$(random_get $LDEV2)
	log_mustnot zpool attach $TESTPOOL $ldev $ldev2
	log_must check_vdev_state $TESTPOOL $ldev2 ""

	log_must zpool destroy -f $TESTPOOL
done

log_pass "Attaching a cache device fails for an existing cache device."
