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
