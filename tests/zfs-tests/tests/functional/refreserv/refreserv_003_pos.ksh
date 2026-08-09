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
#	Verify a snapshot will only be allowed if there is enough free pool
#	space outside of this refreservation.
#
# STRATEGY:
#	1. Setting quota and refreservation
#	2. Verify snapshot can be created, when used =< quota - refreserv
#	3. Verify failed to create snapshot, when used > quota - refreserv
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Verify a snapshot will only be allowed if there is enough " \
	"free space outside of this refreservation."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
log_must zfs set quota=25M $fs
log_must zfs set refreservation=10M $fs

mntpnt=$(get_prop mountpoint $fs)
log_must mkfile 7M $mntpnt/$TESTFILE
log_must zfs snapshot $fs@snap

log_must mkfile 7M $mntpnt/$TESTFILE.2
log_must zfs snapshot $fs@snap2

log_must mkfile 7M $mntpnt/$TESTFILE.3
log_mustnot zfs snapshot $fs@snap3
if datasetexists $fs@snap3 ; then
	log_fail "ERROR: $fs@snap3 should not exists."
fi

log_pass "Verify a snapshot will only be allowed if there is enough " \
	"free space outside of this refreservation."
