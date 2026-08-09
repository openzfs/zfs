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

log_must eval "zfs list $fs > /dev/null"
log_must eval "zfs get mountpoint $fs > /dev/null"
log_must zfs unmount $fs
log_must zfs mount $fs
if ! is_linux; then
	log_must zfs share $fs
	log_must zfs unshare $fs
fi
log_must eval "zfs send -i $snap1 $snap2 > /dev/null"
log_must zfs holds $snap1

log_must eval "zpool history $TESTPOOL > $NEW_HISTORY"
log_must diff $OLD_HISTORY $NEW_HISTORY

log_must zfs release tag $snap1

log_pass "Verify 'zfs list|get|mount|unmount|share|unshare|send' passed."
