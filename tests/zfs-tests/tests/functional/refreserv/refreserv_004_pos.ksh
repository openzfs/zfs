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
. $STF_SUITE/tests/functional/refreserv/refreserv.cfg

#
# DESCRIPTION:
#	Verify refreservation is limited by available space.
#
# STRATEGY:
#	1. Setting quota and refreservation on parent filesystem.
#	2. Get available space on sub-filesystem.
#	3. Verify refreservation is limited by available on it.
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Verify refreservation is limited by available space."
log_onexit cleanup

pool=$TESTPOOL ; fs=$pool/$TESTFS/$TESTFS1 ; subfs=$fs/subfs
log_must zfs create $fs
log_must zfs create $subfs

typeset datasets
if is_global_zone; then
        # The pool root also accounts for changing MOS allocations.
        datasets="$pool/$TESTFS $fs"
else
        datasets="$fs"
fi

for ds in $datasets; do
	log_must zfs set quota=25M $ds
	log_must zfs set refreservation=15M $ds

	typeset -i avail
	log_must zpool sync $pool
	avail=$(get_prop avail $subfs)
	log_must zfs set refreservation=$avail $subfs
	typeset mntpnt
	mntpnt=$(get_prop mountpoint $subfs)
	# File metadata must also fit within the quota.
	log_must mkfile $((avail / 2)) $mntpnt/$TESTFILE

	log_must zpool sync $pool
	((avail = $(get_prop usedbydataset $subfs) + $(get_prop avail $subfs)))
	log_must zfs set refreservation=$avail $subfs
	typeset -i exceed
	((exceed = avail + 1))
	log_mustnot zfs set refreservation=$exceed $subfs
	log_mustnot mkfile $avail $mntpnt/$TESTFILE
	log_must rm $mntpnt/$TESTFILE
	log_must zpool sync $pool

	log_must zfs set quota=none $ds
	log_must zfs set reservation=15M $ds
done

log_pass "Verify refreservation is limited by available space."
