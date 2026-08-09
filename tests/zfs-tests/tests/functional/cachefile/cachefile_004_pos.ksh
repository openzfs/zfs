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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cachefile/cachefile.cfg
. $STF_SUITE/tests/functional/cachefile/cachefile.kshlib

#
# DESCRIPTION:
#	Verify set, export and destroy when cachefile is set on pool.
#
# STRATEGY:
#	1. Create two pools with one same cachefile1.
#	2. Set cachefile of the two pools to another same cachefile2.
#	3. Verify cachefile1 does not exist.
#	4. Export the two pools.
#	5. Verify cachefile2 not exist.
#	6. Import the two pools and set cachefile to cachefile2.
#	7. Destroy the two pools.
#	8. Verify cachefile2 not exist.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2

	mntpnt=$(get_prop mountpoint $TESTPOOL)
	typeset -i i=0
	while ((i < 2)); do
		if [[ -e $mntpnt/vdev$i ]]; then
			log_must rm -f $mntpnt/vdev$i
		fi
		((i += 1))
	done

	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	for file in $CPATH1 $CPATH2 ; do
		if [[ -f $file ]] ; then
			log_must rm $file
		fi
	done
}


log_assert "Verify set, export and destroy when cachefile is set on pool."
log_onexit cleanup

log_must zpool create $TESTPOOL $DISKS

mntpnt=$(get_prop mountpoint $TESTPOOL)
typeset -i i=0
while ((i < 2)); do
	log_must mkfile $MINVDEVSIZE $mntpnt/vdev$i
	eval vdev$i=$mntpnt/vdev$i
	((i += 1))
done

log_must zpool create -o cachefile=$CPATH1 $TESTPOOL1 $vdev0
log_must pool_in_cache $TESTPOOL1 $CPATH1
log_must zpool create -o cachefile=$CPATH1 $TESTPOOL2 $vdev1
log_must pool_in_cache $TESTPOOL2 $CPATH1

log_must zpool set cachefile=$CPATH2 $TESTPOOL1
log_must pool_in_cache $TESTPOOL1 $CPATH2
log_must zpool set cachefile=$CPATH2 $TESTPOOL2
log_must pool_in_cache $TESTPOOL2 $CPATH2
if [[ -s $CPATH1 ]]; then
	log_fail "Verify set when cachefile is set on pool."
fi

log_must zpool export $TESTPOOL1
log_must zpool export $TESTPOOL2
if [[ -s $CPATH2 ]]; then
	log_fail "Verify export when cachefile is set on pool."
fi

log_must zpool import -d $mntpnt $TESTPOOL1
log_must zpool set cachefile=$CPATH2 $TESTPOOL1
log_must pool_in_cache $TESTPOOL1 $CPATH2
log_must zpool import -d $mntpnt $TESTPOOL2
log_must zpool set cachefile=$CPATH2 $TESTPOOL2
log_must pool_in_cache $TESTPOOL2 $CPATH2

log_must zpool destroy $TESTPOOL1
log_must zpool destroy $TESTPOOL2
if [[ -s $CPATH2 ]]; then
	log_fail "Verify destroy when cachefile is set on pool."
fi

log_pass "Verify set, export and destroy when cachefile is set on pool."
