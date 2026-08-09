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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Valid pool names are accepted
#
# STRATEGY:
# 1. Using a list of valid pool names
# 2. Create a filesystem in that pool
# 2. Verify we can set the bootfs to that filesystem
#

verify_runnable "global"

set -A pools "pool.$$" "pool123" "mypool"

function cleanup {
	if poolexists $POOL ; then
		log_must zpool destroy $POOL
	fi
	rm $TESTDIR/bootfs_003.$$.dat
}


log_onexit cleanup

log_assert "Valid pool names are accepted by zpool set bootfs"
mkfile $MINVDEVSIZE $TESTDIR/bootfs_003.$$.dat

typeset -i i=0;

while [ $i -lt "${#pools[@]}" ]
do
	POOL=${pools[$i]}
	log_must zpool create $POOL $TESTDIR/bootfs_003.$$.dat
	log_must zfs create $POOL/$TESTFS

	log_must zpool set bootfs=$POOL/$TESTFS $POOL
	RES=$(zpool get bootfs $POOL | awk 'END {print $3}' )
	if [ $RES != "$POOL/$TESTFS" ]
	then
		log_fail "Expected $RES == $POOL/$TESTFS"
	fi
	log_must zpool destroy $POOL
	i=$(( $i + 1 ))
done

log_pass "Valid pool names are accepted by zpool set bootfs"
