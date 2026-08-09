#!/bin/ksh
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
#	'zfs promote' can successfully promote a volume clone.
#
# STRATEGY:
#	1. Create a volume clone
#	2. Promote the volume clone
#	3. Verify the dependency changed.
#

verify_runnable "global"

function cleanup
{
	if snapexists $csnap; then
		log_must zfs promote $vol
	fi

	destroy_dataset "$snap" "-rR"
}

log_assert "'zfs promote' can promote a volume clone."
log_onexit cleanup

vol=$TESTPOOL/$TESTVOL
snap=$vol@$TESTSNAP
clone=$TESTPOOL/volclone
csnap=$clone@$TESTSNAP

if ! snapexists $snap ; then
	log_must zfs snapshot $snap
	log_must zfs clone $snap $clone
fi

log_must zfs promote $clone

# verify the 'promote' operation
! snapexists $csnap && \
		log_fail "Snapshot $csnap doesn't exist after zfs promote."
snapexists $snap && \
	log_fail "Snapshot $snap is still there after zfs promote."

origin_prop=$(get_prop origin $vol)
[[ "$origin_prop" != "$csnap" ]] && \
	log_fail "The dependency of $vol is not correct."
origin_prop=$(get_prop origin $clone)
[[ "$origin_prop" != "-" ]] && \
	 log_fail "The dependency of $clone is not correct."

log_pass "'zfs promote' can promote volume clone as expected."

