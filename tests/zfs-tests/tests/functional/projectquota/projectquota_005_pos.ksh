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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Check the invalid parameter of zfs get project{obj}quota
#
#
# STRATEGY:
#	1. check the invalid zfs get project{obj}quota to fs
#	2. check the valid zfs get project{obj}quota to snapshots
#

function cleanup
{
	if datasetexists $snap_fs; then
		log_must zfs destroy $snap_fs
	fi

	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs get project{obj}quota"
typeset snap_fs=$QFS@snap

log_must zfs snapshot $snap_fs

set -A no_prjs "mms1234" "ss@#" "root-122"
for prj in "${no_prjs[@]}"; do
	log_must eval "zfs get projectquota@$prj $QFS >/dev/null 2>&1"
	log_must eval "zfs get projectquota@$prj $snap_fs >/dev/null 2>&1"
	log_must eval "zfs get projectobjquota@$prj $QFS >/dev/null 2>&1"
	log_must eval "zfs get projectobjquota@$prj $snap_fs >/dev/null 2>&1"
done

log_pass "Check the invalid parameter of zfs get project{obj}quota"
