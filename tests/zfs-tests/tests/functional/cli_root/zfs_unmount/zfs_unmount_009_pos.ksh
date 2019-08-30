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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that zfs unmount and destroy in a snapshot directory will not cause error.
#
# STRATEGY:
# 1. Create a file in a zfs filesystem, snapshot it and change directory to snapshot directory
# 2. Verify that 'zfs unmount -a'  will fail and 'zfs unmount -fa' will succeed
# 3. Verify 'ls' and 'cd /' will succeed
# 4. 'zfs mount -a' and change directory to snapshot directory again
# 5. Verify that zfs destroy snapshot will succeed
# 6. Verify 'ls' and 'cd /' will succeed
# 7. Create zfs filesystem, create a file, snapshot it and change to snapshot directory
# 8. Verify that zpool destroy the pool will succeed
# 9. Verify 'ls' 'cd /' 'zpool list' and etc will succeed
#

verify_runnable "both"

function cleanup
{
	DISK=${DISKS%% *}

	for fs in $TESTPOOL/$TESTFS $TESTPOOL ; do
		typeset snap=$fs@$TESTSNAP
		if snapexists $snap; then
			log_must zfs destroy $snap
		fi
	done

	if ! poolexists $TESTPOOL && is_global_zone; then
		log_must zpool create $TESTPOOL $DISK
	fi

	if ! datasetexists $TESTPOOL/$TESTFS; then
		log_must zfs create $TESTPOOL/$TESTFS
		log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	fi
}

function restore_dataset
{
	if ! datasetexists $TESTPOOL/$TESTFS ; then
		log_must zfs create $TESTPOOL/$TESTFS
		log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
		log_must cd $TESTDIR
		echo hello > world
		log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP
		log_must cd .zfs/snapshot/$TESTSNAP
	fi
}


log_assert "zfs force unmount and destroy in snapshot directory will not cause error."
log_onexit cleanup

for fs in $TESTPOOL/$TESTFS $TESTPOOL ; do
	typeset snap=$fs@$TESTSNAP
	typeset mtpt=$(get_prop mountpoint $fs)

	log_must cd $mtpt
	echo hello > world
	log_must zfs snapshot $snap
	log_must cd .zfs/snapshot/$TESTSNAP

	log_mustnot zfs unmount -a
	if is_linux; then
		log_mustnot zfs unmount -fa
		log_must ls
	else
		log_must zfs unmount -fa
		log_mustnot ls
	fi
	log_must cd /

	log_must zfs mount -a
	log_must cd $mtpt
	log_must cd .zfs/snapshot/$TESTSNAP

	if is_global_zone || [[ $fs != $TESTPOOL ]] ; then
		if is_linux; then
			log_mustnot zfs destroy -rf $fs
			log_must ls
		else
			log_must zfs destroy -rf $fs
			log_mustnot ls
		fi
		log_must cd /
	fi

	restore_dataset
done

if is_global_zone ; then
	if is_linux; then
		log_mustnot zpool destroy -f $TESTPOOL
		log_must ls
	else
		log_must zpool destroy -f $TESTPOOL
		log_mustnot ls
	fi
	log_must cd /
fi

log_must eval zfs list > /dev/null 2>&1
log_must eval zpool list > /dev/null 2>&1
log_must eval zpool status > /dev/null 2>&1
zpool iostat > /dev/null 2>&1

log_pass "zfs force unmount and destroy in snapshot directory will not cause error."
