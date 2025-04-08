#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
#	'zfs rename' can address the abbreviated snapshot name.
#
# STRATEGY:
#	1. Create pool, fs and snap.
#	2. Verify 'zfs rename' support the abbreviated snapshot name.
#

verify_runnable "both"

function cleanup
{
	datasetexists $snap && destroy_dataset $snap
}

log_assert "'zfs rename' can address the abbreviated snapshot name."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS; snap=$fs@snap
set -A newname "$fs@new-snap" "@new-snap" "new-snap"

log_must zfs snapshot $snap
log_must datasetexists $snap

typeset -i i=0
while ((i < ${#newname[*]} )); do
        log_must zfs rename $snap ${newname[$i]}
	log_must datasetexists ${snap%%@*}@${newname[$i]##*@}
	log_must zfs rename ${snap%%@*}@${newname[$i]##*@} $snap

	((i += 1))
done

log_pass "'zfs rename' address the abbreviated snapshot name passed."
