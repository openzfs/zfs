#!/usr/bin/ksh -p
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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Invalid datasets are rejected as boot property values
#
# STRATEGY:
#
# 1. Create a snapshot and a zvol
# 2. Verify that we can't set the bootfs to those datasets
#

verify_runnable "global"

function cleanup {
	if snapexists $TESTPOOL/$TESTFS@snap
	then
		$ZFS destroy $TESTPOOL/$TESTFS@snap
	fi
	if datasetexists $TESTPOOL/$TESTFS
	then
		log_must $ZFS destroy $TESTPOOL/$TESTFS
	fi
	if datasetexists $TESTPOOL/vol
	then
		log_must $ZFS destroy $TESTPOOL/vol
	fi
	if poolexists $TESTPOOL
	then
		log_must $ZPOOL destroy $TESTPOOL
	fi
}


$ZPOOL set 2>&1 | $GREP bootfs > /dev/null
if [ $? -ne 0 ]
then
	log_unsupported "bootfs pool property not supported on this release."
fi

log_assert "Invalid datasets are rejected as boot property values"
log_onexit cleanup

DISK=${DISKS%% *}

log_must $ZPOOL create $TESTPOOL $DISK
log_must $ZFS create $TESTPOOL/$TESTFS
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snap
log_must $ZFS create -V 10m $TESTPOOL/vol

log_mustnot $ZPOOL set bootfs=$TESTPOOL/$TESTFS@snap $TESTPOOL
log_mustnot $ZPOOL set bootfs=$TESTPOOL/vol $TESTPOOL

log_pass "Invalid datasets are rejected as boot property values"
