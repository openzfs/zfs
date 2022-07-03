#!/bin/ksh -p
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that ZFS volume snapshot could be fscked
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Copy some files and create snapshot
# 3. Verify fsck on the snapshot is OK
#

verify_runnable "global"

volsize=$(zfs get -H -o value volsize $TESTPOOL/$TESTVOL)

function cleanup
{
	snapexists $TESTPOOL/$TESTVOL@snap &&
		destroy_dataset $TESTPOOL/$TESTVOL@snap

	ismounted $TESTDIR $NEWFS_DEFAULT_FS &&
		log_must umount $TESTDIR

	zfs set volsize=$volsize $TESTPOOL/$TESTVOL
}

log_assert "Verify that ZFS volume snapshot could be fscked"
log_onexit cleanup

TESTVOL='testvol'
BLOCKSZ=$(( 1024 * 1024 ))
NUM_WRITES=40

log_must zfs set volsize=128m $TESTPOOL/$TESTVOL

log_must new_fs ${ZVOL_RDEVDIR}/$TESTPOOL/$TESTVOL

log_must mount ${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL $TESTDIR

typeset -i fn=0
typeset -i retval=0

while file_write -o create -f $TESTDIR/testfile$$.$fn \
	    -b $BLOCKSZ -c $NUM_WRITES; do
	(( fn = fn + 1 ))
done

if is_linux || is_freebsd ; then
	sync_all_pools
else
	log_must lockfs -f $TESTDIR
fi

log_must zfs set snapdev=visible $TESTPOOL/$TESTVOL
log_must zfs snapshot $TESTPOOL/$TESTVOL@snap
block_device_wait

fsck -n ${ZVOL_RDEVDIR}/$TESTPOOL/$TESTVOL@snap >/dev/null 2>&1
retval=$?

if [ $retval -ne 0 ] ; then
	if is_linux ; then
		# Linux's fsck returns a different code for this test depending
		# on the version:
		#
		# e2fsprogs-1.43.3 (Fedora 25 and older): returns 4
		# e2fsprogs-1.43.4 (Fedora 26): returns 8
		#
		if [ $retval -ne 4 -a $retval -ne 8 ] ; then
			log_fail "fsck exited with wrong value $retval"
		fi
	else
		if [ $retval -ne 39 ] ; then
			log_fail "fsck exited with wrong value $retval"
		fi
	fi
fi

log_pass "Verify that ZFS volume snapshot could be fscked"
