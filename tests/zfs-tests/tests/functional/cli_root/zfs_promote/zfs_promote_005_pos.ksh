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
		destroy_dataset $clone
		destroy_dataset $fssnap
	fi
	if datasetexists $clone ; then
		log_must $ZFS promote $fs
		destroy_dataset $clone
		destroy_dataset $fssnap
	fi
}

log_assert "The original fs was unmounted, 'zfs promote' still should succeed."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
clone=$TESTPOOL/$TESTCLONE
fssnap=$fs@fssnap

log_must $ZFS snapshot $fssnap
log_must $ZFS clone $fssnap $clone
log_must $ZFS unmount $fs
log_must $ZFS promote $clone
log_must $ZFS unmount $clone
log_must $ZFS promote $fs

log_pass "Unmount original fs, 'zfs promote' passed."
