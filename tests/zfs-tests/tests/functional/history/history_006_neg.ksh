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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/history/history_common.kshlib
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify the following zfs subcommands are not logged.
#	list, get, holds, mount, unmount, share, unshare, send
#
# STRATEGY:
#	1. Create a test pool.
#	2. Separately invoke zfs list|get|holds|mount|unmount|share|unshare|send
#	3. Verify they were not recorded in pool history.
#

verify_runnable "global"

function cleanup
{
	datasetexists $fs && destroy_dataset $fs -rf
	log_must zfs create $fs
}

log_assert "Verify 'zfs list|get|holds|mount|unmount|share|unshare|send' " \
    "will not be logged."
log_onexit cleanup

# Create initial test environment
fs=$TESTPOOL/$TESTFS; snap1=$fs@snap1; snap2=$fs@snap2
if ! is_linux; then
	log_must zfs set sharenfs=on $fs
fi
log_must zfs snapshot $snap1
log_must zfs hold tag $snap1
log_must zfs snapshot $snap2

# Save initial TESTPOOL history
log_must eval "zpool history $TESTPOOL > $OLD_HISTORY"

log_must zfs list $fs > /dev/null
log_must zfs get mountpoint $fs > /dev/null
log_must zfs unmount $fs
log_must zfs mount $fs
if ! is_linux; then
	log_must zfs share $fs
	log_must zfs unshare $fs
fi
log_must zfs send -i $snap1 $snap2 > /dev/null
log_must zfs holds $snap1

log_must eval "zpool history $TESTPOOL > $NEW_HISTORY"
log_must diff $OLD_HISTORY $NEW_HISTORY

log_must zfs release tag $snap1

log_pass "Verify 'zfs list|get|mount|unmount|share|unshare|send' passed."
