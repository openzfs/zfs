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
