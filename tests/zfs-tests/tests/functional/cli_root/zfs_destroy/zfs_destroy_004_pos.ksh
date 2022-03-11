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
#	Verify 'zfs destroy -f' succeeds as root.
#
# STRATEGY:
#	1. Create filesystem in the storage pool
#	2. Set mountpoint for the filesystem and make it busy
#	3. Verify that 'zfs destroy' fails to destroy the filesystem
#	4. Verify 'zfs destroy -f' succeeds to destroy the filesystem.
#

verify_runnable "both"

function cleanup
{
	cd $olddir

	datasetexists $clone && destroy_dataset $clone -f
	snapexists $snap && destroy_dataset $snap -f

	for fs in $fs1 $fs2; do
		datasetexists $fs && destroy_dataset $fs -f
	done

	for dir in $TESTDIR1 $TESTDIR2; do
		[[ -d $dir ]] && \
			log_must rm -rf $dir
	done
}

log_assert "Verify that 'zfs destroy -f' succeeds as root. "

log_onexit cleanup

#
# Preparations for testing
#
olddir=$PWD

for dir in $TESTDIR1 $TESTDIR2; do
	[[ ! -d $dir ]] && \
		log_must mkdir -p $dir
done

fs1=$TESTPOOL/$TESTFS1
mntp1=$TESTDIR1
fs2=$TESTPOOL/$TESTFS2
snap=$TESTPOOL/$TESTFS2@snap
clone=$TESTPOOL/$TESTCLONE
mntp2=$TESTDIR2

#
# Create filesystem and clone in the storage pool,  mount them and
# make the mountpoint busy
#
for fs in $fs1 $fs2; do
	log_must zfs create $fs
done

log_must zfs snapshot $snap
log_must zfs clone $snap $clone

log_must zfs set mountpoint=$mntp1 $fs1
log_must zfs set mountpoint=$mntp2 $clone

for arg in "$fs1 $mntp1" "$clone $mntp2"; do
	read -r fs mntp <<<"$arg"

	log_note "Verify that 'zfs destroy' fails to" \
			"destroy filesystem when it is busy."
	cd $mntp
	log_mustnot zfs destroy $fs

	if is_linux; then
		log_mustnot zfs destroy -f $fs
		datasetnonexists $fs && \
		    log_fail "'zfs destroy -f' destroyed busy filesystem."
	else
		log_must zfs destroy -f $fs
		datasetexists $fs && \
		    log_fail "'zfs destroy -f' fail to destroy busy filesystem."
	fi

	cd $olddir
done

log_pass "'zfs destroy -f' succeeds as root."
