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
# Copyright (c) 2013 by Delphix. All rights reserved.
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
	if is_global_zone ; then
		log_must $ZFS set refreservation=none $TESTPOOL
	fi
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Verify refreservation is limited by available space."
log_onexit cleanup

pool=$TESTPOOL ; fs=$pool/$TESTFS ; subfs=$fs/subfs
log_must $ZFS create $subfs

typeset datasets
if is_global_zone; then
        datasets="$pool $fs"
else
        datasets="$fs"
fi

for ds in $datasets; do
	log_must $ZFS set quota=25M $ds
	log_must $ZFS set refreservation=15M $ds

	typeset -i avail
	avail=$(get_prop avail $subfs)
	log_must $ZFS set refreservation=$avail $subfs
	typeset mntpnt
	mntpnt=$(get_prop mountpoint $subfs)
	log_must $MKFILE $avail $mntpnt/$TESTFILE

	typeset -i exceed
	((exceed = avail + 1))
	log_mustnot $ZFS set refreservation=$exceed $subfs
	log_mustnot $MKFILE $avail $mntpnt/$TESTFILE

	log_must $ZFS set quota=none $ds
	log_must $ZFS set reservation=15M $ds
done

log_pass "Verify refreservation is limited by available space."
