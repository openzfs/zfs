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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_rename/zfs_rename.kshlib

#
# DESCRIPTION:
#	Rename dataset, verify that the data haven't changed.
#
# STRATEGY:
#	1. Create random data and copy to dataset.
#	2. Perform renaming commands.
#	3. Verify that the data haven't changed.
#

verify_runnable "both"

function cleanup
{
	destroy_dataset -Rf $TESTPOOL/$TESTFS

	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	$RM -f $SRC_FILE $DST_FILE
}

function target_obj
{
	typeset dtst=$1

	typeset obj
	typeset type=$(get_prop type $dtst)
	if [[ $type == "filesystem" ]]; then
		obj=$(get_prop mountpoint $dtst)/${SRC_FILE##*/}
	elif [[ $type == "volume" ]]; then
		obj=$ZVOL_DEVDIR/$dtst
	fi

	echo $obj
}

log_assert "Rename dataset, verify that the data haven't changed."
log_onexit cleanup

# Generate random data
#
BS=512 ; CNT=2048
SRC_FILE=/tmp/srcfile.$$
DST_FILE=/tmp/dstfile.$$
log_must $DD if=/dev/urandom of=$SRC_FILE bs=$BS count=$CNT

fs=$TESTPOOL/$TESTFS/fs.$$
fsclone=$TESTPOOL/$TESTFS/fsclone.$$
log_must $ZFS create $fs

obj=$(target_obj $fs)
log_must $CP $SRC_FILE $obj

snap=${fs}@snap.$$
log_must $ZFS snapshot $snap
log_must $ZFS clone $snap $fsclone

# Rename dataset & clone
#
log_must $ZFS rename $fs ${fs}-new
log_must $ZFS rename $fsclone ${fsclone}-new

# Compare source file and target file
#
obj=$(target_obj ${fs}-new)
log_must $DIFF $SRC_FILE $obj
obj=$(target_obj ${fsclone}-new)
log_must $DIFF $SRC_FILE $obj

# Rename snapshot and re-clone dataset
#
log_must $ZFS rename ${fs}-new $fs
log_must $ZFS rename $snap ${snap}-new
log_must $ZFS clone ${snap}-new $fsclone

# Compare source file and target file
#
obj=$(target_obj $fsclone)
log_must $DIFF $SRC_FILE $obj

if is_global_zone; then
	vol=$TESTPOOL/$TESTFS/vol.$$ ;	volclone=$TESTPOOL/$TESTFS/volclone.$$
	log_must $ZFS create -V 100M $vol
	[[ -n "$LINUX" ]] && sleep 1

	obj=$(target_obj $vol)
	log_must $DD if=$SRC_FILE of=$obj bs=$BS count=$CNT

	snap=${vol}@snap.$$
	log_must $ZFS snapshot $snap
	log_must $ZFS clone $snap $volclone

	# Rename dataset & clone
	log_must $ZFS rename $vol ${vol}-new
	log_must $ZFS rename $volclone ${volclone}-new

	# Compare source file and target file
	obj=$(target_obj ${vol}-new)
	log_must $DD if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must $DIFF $SRC_FILE $DST_FILE
	obj=$(target_obj ${volclone}-new)
	log_must $DD if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must $DIFF $SRC_FILE $DST_FILE

	# Rename snapshot and re-clone dataset
	log_must $ZFS rename ${vol}-new $vol
	log_must $ZFS rename $snap ${snap}-new
	log_must $ZFS clone ${snap}-new $volclone

	# Compare source file and target file
	obj=$(target_obj $volclone)
	log_must $DD if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must $DIFF $SRC_FILE $DST_FILE
fi

log_pass "Rename dataset, the data haven't changed passed."
