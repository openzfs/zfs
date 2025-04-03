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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
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
	datasetexists $TESTPOOL/$TESTFS && \
		destroy_dataset $TESTPOOL/$TESTFS -Rf
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	rm -f $SRC_FILE $DST_FILE
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
SRC_FILE=$TESTDIR/srcfile.$$
DST_FILE=$TESTDIR/dstfile.$$
log_must dd if=/dev/urandom of=$SRC_FILE bs=$BS count=$CNT

fs=$TESTPOOL/$TESTFS/fs.$$
fsclone=$TESTPOOL/$TESTFS/fsclone.$$
log_must zfs create $fs

obj=$(target_obj $fs)
log_must cp $SRC_FILE $obj

snap=${fs}@snap.$$
log_must zfs snapshot $snap
log_must zfs clone $snap $fsclone

# Rename dataset & clone
#
log_must zfs rename $fs ${fs}-new
log_must zfs rename $fsclone ${fsclone}-new

# Compare source file and target file
#
obj=$(target_obj ${fs}-new)
log_must diff $SRC_FILE $obj
obj=$(target_obj ${fsclone}-new)
log_must diff $SRC_FILE $obj

# Rename snapshot and re-clone dataset
#
log_must zfs rename ${fs}-new $fs
log_must zfs rename $snap ${snap}-new
log_must zfs clone ${snap}-new $fsclone

# Compare source file and target file
#
obj=$(target_obj $fsclone)
log_must diff $SRC_FILE $obj

if is_global_zone; then
	vol=$TESTPOOL/$TESTFS/vol.$$ ;	volclone=$TESTPOOL/$TESTFS/volclone.$$
	log_must zfs create -V 100M $vol

	obj=$(target_obj $vol)
	block_device_wait $obj
	log_must dd if=$SRC_FILE of=$obj bs=$BS count=$CNT

	snap=${vol}@snap.$$
	log_must zfs snapshot $snap
	log_must zfs clone $snap $volclone

	# Rename dataset & clone
	log_must zfs rename $vol ${vol}-new
	log_must zfs rename $volclone ${volclone}-new

	# Compare source file and target file
	obj=$(target_obj ${vol}-new)
	block_device_wait $obj
	log_must dd if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must diff $SRC_FILE $DST_FILE
	obj=$(target_obj ${volclone}-new)
	block_device_wait $obj
	log_must dd if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must diff $SRC_FILE $DST_FILE

	# Rename snapshot and re-clone dataset
	log_must zfs rename ${vol}-new $vol
	log_must zfs rename $snap ${snap}-new
	log_must zfs clone ${snap}-new $volclone

	# Compare source file and target file
	obj=$(target_obj $volclone)
	block_device_wait $obj
	log_must dd if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must diff $SRC_FILE $DST_FILE
fi

log_pass "Rename dataset, the data haven't changed passed."
