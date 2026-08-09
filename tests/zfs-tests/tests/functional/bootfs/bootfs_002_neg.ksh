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
# Copyright 2015 Nexenta Systems, Inc.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

#
# Copyright (c) 2012, 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Invalid datasets are rejected as boot property values
#
# STRATEGY:
#
# 1. Create a zvol 
# 2. Verify that we can't set the bootfs to that dataset
#

verify_runnable "global"

function cleanup {
	datasetexists $TESTPOOL/vol && destroy_dataset $TESTPOOL/vol
	poolexists $TESTPOOL && log_must zpool destroy $TESTPOOL

	if [[ -f $VDEV ]]; then
		log_must rm -f $VDEV
	fi
}


log_assert "Invalid datasets are rejected as boot property values"
log_onexit cleanup

typeset VDEV=$TESTDIR/bootfs_002_neg_a.$$.dat

log_must mkfile 400m $VDEV
create_pool "$TESTPOOL" "$VDEV"
log_must zfs create -V 10m $TESTPOOL/vol
block_device_wait

log_mustnot zpool set bootfs=$TESTPOOL/vol $TESTPOOL

log_pass "Invalid datasets are rejected as boot property values"
