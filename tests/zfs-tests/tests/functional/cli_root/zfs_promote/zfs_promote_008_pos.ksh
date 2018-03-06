#!/bin/ksh
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

