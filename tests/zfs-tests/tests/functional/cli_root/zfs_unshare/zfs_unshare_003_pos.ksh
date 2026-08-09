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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that a file system and its dependent are unshared when turn off sharenfs
# property.
#
# STRATEGY:
# 1. Create a file system
# 2. Set the sharenfs property on the file system
# 3. Create a snapshot
# 4. Verify that both are shared
# 5. Turn off the sharenfs property
# 6. Verify that both are unshared.
#

verify_runnable "global"

function cleanup
{
	snapexists $TESTPOOL/$TESTFS@snapshot && \
		destroy_dataset $TESTPOOL/$TESTFS@snapshot

	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
}

#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# unshare the mountpoint and then verify a snapshot of the mounpoint
# is also unshared.
#
function test_snap_unshare # <mntp> <filesystem>
{
        typeset mntp=$1
        typeset filesystem=$2
	typeset prop_value

	prop_value=$(get_prop "sharenfs" $filesystem)

	if [[ $prop_value == "off" ]]; then
		is_shared $mntp || unshare_nfs $mntp
		log_must zfs set sharenfs=on $filesystem
	fi

	log_must zfs set sharenfs=off $filesystem

	not_shared $mntp || \
		log_fail "File system $filesystem is shared (set sharenfs)."

	not_shared $mntp@snapshot || \
	    log_fail "Snapshot $mntpt@snapshot is shared (set sharenfs)."
}

log_assert "Verify that a file system and its dependent are unshared."
log_onexit cleanup

log_must zfs snapshot $TESTPOOL/$TESTFS@snapshot
test_snap_unshare $TESTDIR $TESTPOOL/$TESTFS

log_pass "A file system and its dependent are both unshared as expected."
