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
