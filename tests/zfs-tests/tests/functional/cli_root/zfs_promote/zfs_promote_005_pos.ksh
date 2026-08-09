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
#	The original fs was unmounted, 'zfs promote' still should succeed.
#
# STRATEGY:
#	1. Create pool, fs and snapshot.
#	2. Create clone of fs.
#	3. Unmount fs, then verify 'zfs promote' clone still succeed.
#

verify_runnable "both"

function cleanup
{
	if datasetexists $fssnap ; then
		datasetexists $clone && destroy_dataset $clone
		destroy_dataset $fssnap
	fi
	if datasetexists $clone ; then
		log_must zfs promote $fs
		log_must zfs destroy $clone
		log_must zfs destroy $fssnap
	fi
}

log_assert "The original fs was unmounted, 'zfs promote' still should succeed."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
clone=$TESTPOOL/$TESTCLONE
fssnap=$fs@fssnap

log_must zfs snapshot $fssnap
log_must zfs clone $fssnap $clone
log_must zfs unmount $fs
log_must zfs promote $clone
log_must zfs unmount $clone
log_must zfs promote $fs

log_pass "Unmount original fs, 'zfs promote' passed."
