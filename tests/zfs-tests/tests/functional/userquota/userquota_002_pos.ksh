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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       the userquota and groupquota can be set during zpool or zfs creation"
#
#
# STRATEGY:
#       1. Set userquota and groupquota via "zpool -O or zfs create -o"
#

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL1; then
		log_must zpool destroy $TESTPOOL1
	fi

	if [[ -f $pool_vdev ]]; then
		rm -f $pool_vdev
	fi
}

log_onexit cleanup

log_assert \
	"the userquota and groupquota can be set during zpool,zfs creation"

typeset pool_vdev=$TEST_BASE_DIR/pool_dev.$$

log_must mkfile 500m $pool_vdev

if poolexists $TESTPOOL1; then
	zpool destroy $TESTPOOL1
fi

log_must zpool create -O userquota@$QUSER1=$UQUOTA_SIZE \
	-O groupquota@$QGROUP=$GQUOTA_SIZE $TESTPOOL1 $pool_vdev

log_must eval "zfs list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL1 > /dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL1 "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL1 "$GQUOTA_SIZE"

log_must zfs create -o userquota@$QUSER1=$UQUOTA_SIZE \
	-o groupquota@$QGROUP=$GQUOTA_SIZE $TESTPOOL1/fs

log_must eval "zfs list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL1 > /dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL1/fs "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL1/fs "$GQUOTA_SIZE"

log_pass \
	"the userquota and groupquota can be set during zpool,zfs creation"
