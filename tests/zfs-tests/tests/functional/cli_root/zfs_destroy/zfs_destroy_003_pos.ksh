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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

#
# DESCRIPTION:
#	Verify 'zfs destroy [-rR]' succeeds as root.
#
# STRATEGY:
#	1. Create two datasets in the storage pool
#	2. Create fs,vol,ctr,snapshot and clones of snapshot in the two datasets
#	3. Create clone in the second dataset for the snapshot in the first dataset
#	4. Verify 'zfs destroy -r' fails to destroy dataset with clone outside it
#	5. Verify 'zfs destroy -R' succeeds to destroy dataset with clone outside it
#	6. Verify 'zfs destroy -r' succeeds to destroy dataset without clone outside it.
#

verify_runnable "both"

function cleanup
{
	for obj in $ctr2 $ctr1 $ctr; do
		datasetexists $obj && \
			log_must zfs destroy -Rf $obj
	done

	for mntp in $TESTDIR1 $TESTDIR2; do
		[[ -d $mntp ]] && \
			log_must rm -rf $mntp
	done
}

log_assert "Verify that 'zfs destroy [-rR]' succeeds as root. "

log_onexit cleanup

#
# Preparations for testing
#
for dir in $TESTDIR1 $TESTDIR2; do
	[[ ! -d $dir ]] && \
		log_must mkdir -p $dir
done

ctr=$TESTPOOL/$TESTCTR
ctr1=$TESTPOOL/$TESTCTR1
ctr2=$ctr/$TESTCTR2
ctr3=$ctr1/$TESTCTR2
child_fs=$ctr/$TESTFS1
child_fs1=$ctr1/$TESTFS2
child_fs_mntp=$TESTDIR1
child_fs1_mntp=$TESTDIR2
child_vol=$ctr/$TESTVOL
child_vol1=$ctr1/$TESTVOL
child_fs_snap=$child_fs@snap
child_fs1_snap=$child_fs1@snap
child_fs_snap_clone=$ctr/$TESTCLONE
child_fs_snap_clone1=$ctr1/${TESTCLONE}_across_ctr
child_fs_snap_clone2=$ctr2/$TESTCLONE2
child_fs1_snap_clone=$ctr1/$TESTCLONE1
child_fs1_snap_clone1=$ctr/${TESTCLONE1}_across_ctr

#
# Create two datasets in the storage pool
#
log_must zfs create $ctr
log_must zfs create $ctr1

#
# Create children datasets fs,vol,snapshot in the datasets, and
# clones across two datasets
#
log_must zfs create $ctr2

for fs in $child_fs $child_fs1; do
	log_must zfs create $fs
done

log_must zfs set mountpoint=$child_fs_mntp $child_fs
log_must zfs set mountpoint=$child_fs1_mntp $child_fs1

for snap in $child_fs_snap $child_fs1_snap; do
	log_must zfs snapshot $snap
done

if is_global_zone ; then
	for vol in $child_vol $child_vol1; do
		log_must zfs create -V $VOLSIZE $vol
	done
fi

for clone in $child_fs_snap_clone $child_fs_snap_clone1; do
	log_must zfs clone $child_fs_snap $clone
done


for clone in $child_fs1_snap_clone $child_fs1_snap_clone1; do
	log_must zfs clone $child_fs1_snap $clone
done

log_note "Verify that 'zfs destroy -r' fails to destroy dataset " \
	"with dependent clone outside it."

for obj in $child_fs $child_fs1 $ctr $ctr1; do
	log_mustnot zfs destroy -r $obj
	datasetexists $obj || \
		log_fail "'zfs destroy -r' fails to keep dependent " \
			"clone outside the hierarchy."
done


log_note "Verify that 'zfs destroy -R' succeeds to destroy dataset " \
	"with dependent clone outside it."

log_must zfs destroy -R $ctr1
datasetexists $ctr1 && \
	log_fail "'zfs destroy -R' fails to destroy dataset with clone outside it."

log_note "Verify that 'zfs destroy -r' succeeds to destroy dataset " \
	"without dependent clone outside it."

log_must zfs destroy -r $ctr
datasetexists $ctr && \
	log_fail "'zfs destroy -r' fails to destroy dataset with clone outside it."

log_pass "'zfs destroy [-rR] succeeds as root."
