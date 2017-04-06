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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/refreserv/refreserv.cfg

#
# DESCRIPTION:
#	Reservations are enforced using the maximum of 'reserv' and 'refreserv'
#
# STRATEGY:
#	1. Setting quota for parent filesystem.
#	2. Setting reservation and refreservation for sub-filesystem.
#	3. Verify the sub-fs reservation are enforced by the maximum of 'reserv'
#	   and 'refreserv'.
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Reservations are enforced using the maximum of " \
	"'reserv' and 'refreserv'"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS ; subfs=$fs/subfs
log_must zfs create $subfs
log_must zfs set quota=25M $fs

log_must zfs set reserv=10M $subfs
log_must zfs set refreserv=20M $subfs
mntpnt=$(get_prop mountpoint $fs)
log_mustnot mkfile 15M $mntpnt/$TESTFILE

log_must rm -f $mntpnt/$TESTFILE

log_must zfs set reserv=20M $subfs
log_must zfs set refreserv=10M $subfs
log_mustnot mkfile 15M $mntpnt/$TESTFILE

log_pass "Reservations are enforced using the maximum of " \
	"'reserv' and 'refreserv'"
