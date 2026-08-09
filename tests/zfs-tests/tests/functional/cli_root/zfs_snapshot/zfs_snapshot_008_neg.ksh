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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_snapshot/zfs_snapshot.cfg

#
# DESCRIPTION:
#	'zfs snapshot pool1@snap pool2@snap' should fail since both snapshots
#	are not in the same pool.
#
# STRATEGY:
#	1. Create 2 separate zpools, zpool name lengths must be the same.
#	2. Attempt to simultaneously create a snapshot of each pool.
#	3. Verify the snapshot creation failed.
#

verify_runnable "both"

function cleanup
{
	for pool in $SNAPPOOL1 $SNAPPOOL2 ; do
		if poolexists $pool ; then
			log_must zpool destroy -f $pool
		fi
	done

	for dev in $SNAPDEV1 $SNAPDEV2 ; do
		if [[ -f $dev ]] ; then
			log_must rm -f $dev
		fi
	done
}

log_assert "'zfs snapshot pool1@snap1 pool2@snap2' should fail since snapshots are in different pools."
log_onexit cleanup

log_must mkfile $MINVDEVSIZE $SNAPDEV1
log_must mkfile $MINVDEVSIZE $SNAPDEV2

log_must zpool create $SNAPPOOL1 $SNAPDEV1
log_must zpool create $SNAPPOOL2 $SNAPDEV2

log_mustnot zfs snapshot $SNAPPOOL1@snap1 $SNAPPOOL2@snap2

log_pass "'zfs snapshot pool1@snap1 pool2@snap2' should fail since snapshots are in different pools."
