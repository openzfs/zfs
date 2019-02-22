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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	The project{obj}quota can be set during zpool or zfs creation
#
#
# STRATEGY:
#	1. Set project{obj}quota via "zpool -O or zfs create -o"
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

log_assert "The project{obj}quota can be set during zpool,zfs creation"

typeset pool_vdev=$TEST_BASE_DIR/pool_dev.$$

log_must mkfile 500m $pool_vdev

if poolexists $TESTPOOL1; then
	zpool destroy $TESTPOOL1
fi

log_must zpool create -O projectquota@$PRJID1=$PQUOTA_LIMIT \
	-O projectobjquota@$PRJID2=$PQUOTA_OBJLIMIT $TESTPOOL1 $pool_vdev

log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL1 > /dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL1 "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL1 "$PQUOTA_OBJLIMIT"

log_must zfs create -o projectquota@$PRJID1=$PQUOTA_LIMIT \
	-o projectobjquota@$PRJID2=$PQUOTA_OBJLIMIT $TESTPOOL1/fs

log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL1 > /dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL1/fs "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL1/fs "$PQUOTA_OBJLIMIT"

log_pass "The project{obj}quota can be set during zpool,zfs creation"
