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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Check the invalid parameter of zfs get defaultproject{obj}quota
#
#
# STRATEGY:
#	1. check the invalid zfs get defaultproject{obj}quota to fs
#	2. check the valid zfs get defaultproject{obj}quota to snapshots
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs get defaultproject{obj}quota"
typeset snap_fs=$QFS@snap

log_must zfs snapshot $snap_fs

set -A no_prjs "mms1234" "ss@#" "root-122"
for prj in "${no_prjs[@]}"; do
	log_must eval "zfs get defaultprojectquota $QFS >/dev/null 2>&1"
	log_must eval "zfs get defaultprojectquota $snap_fs >/dev/null 2>&1"
	log_must eval "zfs get defaultprojectobjquota $QFS >/dev/null 2>&1"
	log_must eval "zfs get defaultprojectobjquota $snap_fs >/dev/null 2>&1"
done

log_pass "Check the invalid parameter of zfs get defaultproject{obj}quota"
