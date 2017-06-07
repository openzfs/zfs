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
# Copyright (c) 2013 by Delphix. All rights reserved.
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
#	3. Verify they were not recored in pool history.
#

verify_runnable "global"

function cleanup
{
	if datasetexists $fs ; then
		log_must $ZFS destroy -rf $fs
	fi
	log_must $ZFS create $fs
}

log_assert "Verify 'zfs list|get|holds|mount|unmount|share|unshare|send' " \
    "will not be logged."
log_onexit cleanup

# Create initial test environment
fs=$TESTPOOL/$TESTFS; snap1=$fs@snap1; snap2=$fs@snap2
log_must $ZFS set sharenfs=on $fs
log_must $ZFS snapshot $snap1
log_must $ZFS hold tag $snap1
log_must $ZFS snapshot $snap2

# Save initial TESTPOOL history
log_must eval "$ZPOOL history $TESTPOOL > $OLD_HISTORY"

log_must $ZFS list $fs > /dev/null
log_must $ZFS get mountpoint $fs > /dev/null
log_must $ZFS unmount $fs
log_must $ZFS mount $fs
log_must $ZFS share $fs
log_must $ZFS unshare $fs
log_must $ZFS send -i $snap1 $snap2 > /dev/null
log_must $ZFS holds $snap1

log_must eval "$ZPOOL history $TESTPOOL > $NEW_HISTORY"
log_must $DIFF $OLD_HISTORY $NEW_HISTORY

log_must $ZFS release tag $snap1

log_pass "Verify 'zfs list|get|mount|unmount|share|unshare|send' passed."
