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
	datasetexists $snap_fs && destroy_dataset $snap_fs

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
