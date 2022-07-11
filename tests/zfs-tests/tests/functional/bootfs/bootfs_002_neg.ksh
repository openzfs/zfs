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
