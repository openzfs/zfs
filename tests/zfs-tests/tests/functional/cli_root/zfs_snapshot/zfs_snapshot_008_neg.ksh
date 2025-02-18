#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
