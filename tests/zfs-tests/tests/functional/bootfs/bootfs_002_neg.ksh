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
# Copyright 2015 Nexenta Systems, Inc.
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
	if datasetexists $TESTPOOL/vol
	then
		log_must $ZFS destroy $TESTPOOL/vol
	fi
	if poolexists $TESTPOOL
	then
		log_must $ZPOOL destroy $TESTPOOL
	fi
	if [[ -f $VDEV ]]; then
		log_must $RM -f $VDEV
	fi
}


$ZPOOL set 2>&1 | $GREP bootfs > /dev/null
if [ $? -ne 0 ]
then
	log_unsupported "bootfs pool property not supported on this release."
fi

log_assert "Invalid datasets are rejected as boot property values"
log_onexit cleanup

typeset VDEV=$TESTDIR/bootfs_002_neg_a.$$.dat

log_must $MKFILE 400m $VDEV
create_pool "$TESTPOOL" "$VDEV"
log_must $ZFS create -V 10m $TESTPOOL/vol
block_device_wait

log_mustnot $ZPOOL set bootfs=$TESTPOOL/vol $TESTPOOL

log_pass "Invalid datasets are rejected as boot property values"
