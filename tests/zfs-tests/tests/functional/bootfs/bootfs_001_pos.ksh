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
# Valid datasets and snapshots are accepted as bootfs property values
#
# STRATEGY:
# 1. Create a set of datasets and snapshots in a test pool
# 2. Try setting them as boot filesystems
#

verify_runnable "global"

function cleanup {
	if poolexists $TESTPOOL ; then
		log_must zpool destroy $TESTPOOL
	fi

	if [[ -f $VDEV ]]; then
		log_must rm -f $VDEV
	fi
}

log_assert "Valid datasets are accepted as bootfs property values"
log_onexit cleanup

typeset VDEV=$TESTDIR/bootfs_001_pos_a.$$.dat

log_must mkfile $MINVDEVSIZE $VDEV
create_pool "$TESTPOOL" "$VDEV"
log_must zfs create $TESTPOOL/$TESTFS

log_must zfs snapshot $TESTPOOL/$TESTFS@snap
log_must zfs clone $TESTPOOL/$TESTFS@snap $TESTPOOL/clone

log_must zpool set bootfs=$TESTPOOL/$TESTFS $TESTPOOL
log_must zpool set bootfs=$TESTPOOL/$TESTFS@snap $TESTPOOL
log_must zpool set bootfs=$TESTPOOL/clone $TESTPOOL

log_must zfs promote $TESTPOOL/clone
log_must zpool set bootfs=$TESTPOOL/clone $TESTPOOL
log_pass "Valid datasets are accepted as bootfs property values"
