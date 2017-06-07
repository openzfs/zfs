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
# Copyright 2016, loli10K. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_send/zfs_send.cfg

#
# DESCRIPTION:
#	Verify 'zfs send' can create valid replication send streams even when
#	we're missing snapshots in the dataset hierarchy.
#
# STRATEGY:
#	1. Create a child fs first and then only snapshot the parent
#	2. Create a full replication send stream and verify it can be received
#	3. Create a snapshot first and then a child fs
#	4. Create another replication send stream and verify it can be received
#

verify_runnable "both"

function cleanup
{
	for snap in $snap1 $snap2 $dsnap1 $dsnap2; do
		snapexists $snap && \
			log_must $ZFS destroy -f $snap
	done

	for ds in $src1 $src2 $dst1 $dst2; do
		datasetexists $ds && \
			log_must $ZFS destroy -Rf $ds
	done

	for file in $bkup1 $bkup2; do
		[[ -e $file ]] && log_must $RM -f $file
	done
}

log_assert "Verify 'zfs send' can create replication send streams."
log_onexit cleanup

src1=$TESTPOOL/$TESTFS/src1
src1child=$src1/src1child
src2=$TESTPOOL/$TESTFS/src2
src2child=$src2/src2child
snap1=$src1@snap1
snap2=$src2@snap2
bkup1=/var/tmp/bkup1.$$
bkup2=/var/tmp/bkup2.$$
dst1=$TESTPOOL/$TESTFS/dst1
dst1child=$dst1/dst1child
dst2=$TESTPOOL/$TESTFS/dst2
dst2child=$dst2/dst2child
dsnap1=$dst1@snap1
dsnap2=$dst2@snap2

log_note "Verify 'zfs send' warns about missing snapshots for datasets"\
	"created before but still create a valid send stream"

log_must $ZFS create $src1
log_must $ZFS set mountpoint=none $src1
log_must $ZFS create $src1child
log_must $ZFS snapshot $snap1
log_mustnot eval "$ZFS send -R $snap1 > $bkup1"
log_must eval "$ZFS receive $dst1 < $bkup1"
receive_check $dsnap1

log_note "Verify 'zfs send' warns about missing snapshots for datasets"\
	"created after but still create a valid send stream"

log_must $ZFS create $src2
log_must $ZFS set mountpoint=none $src2
log_must $ZFS snapshot $snap2
log_must $ZFS create $src2child
log_mustnot eval "$ZFS send -R $snap2 > $bkup2"
log_must eval "$ZFS receive $dst2 < $bkup2"
receive_check $dsnap2

log_pass "Verify 'zfs send' can create replication send streams."
