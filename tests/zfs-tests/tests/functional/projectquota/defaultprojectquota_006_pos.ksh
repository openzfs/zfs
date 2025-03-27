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
#	Check defaultproject{obj}quota to snapshot that:
#	1) can not set defaultproject{obj}quota to snapshot directly
#	2) snapshot can inherit the parent fs's defaultproject{obj}quota
#	3) the defaultproject{obj}quota will not change even the parent quota changed.
#
#
# STRATEGY:
#	1. create a snapshot of a fs
#	2. set the defaultproject{obj}quota to snapshot and expect fail
#	3. set defaultproject{obj}quota to fs and check the snapshot
#	4. re-set defaultproject{obj}quota to fs and check the snapshot's value
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Check the snapshot's defaultproject{obj}quota"
typeset snap_fs=$QFS@snap


log_must zfs set defaultprojectquota=$PQUOTA_LIMIT $QFS
log_must check_quota "defaultprojectquota" $QFS "$PQUOTA_LIMIT"

log_must zfs set defaultprojectobjquota=$PQUOTA_OBJLIMIT $QFS
log_must check_quota "defaultprojectobjquota" $QFS "$PQUOTA_OBJLIMIT"

log_must zfs snapshot $snap_fs

log_note "check the snapshot $snap_fs defaultproject{obj}quota"
log_must check_quota "defaultprojectquota" $snap_fs "$PQUOTA_LIMIT"
log_must check_quota "defaultprojectobjquota" $snap_fs "$PQUOTA_OBJLIMIT"

log_note  "set defaultproject{obj}quota to $snap_fs which will fail"
log_mustnot zfs set defaultprojectquota=100m $snap_fs
log_mustnot zfs set defaultprojectobjquota=100 $snap_fs

log_note "change the parent's project{obj}quota"
log_must zfs set defaultprojectquota=$((PQUOTA_LIMIT * 2)) $QFS
log_must zfs set defaultprojectobjquota=50 $QFS

log_must check_quota "defaultprojectquota" $QFS $((PQUOTA_LIMIT * 2))
log_must check_quota "defaultprojectobjquota" $QFS 50

log_note "check the snapshot $snap_fs defaultproject{obj}quota"
log_must check_quota "defaultprojectquota" $snap_fs "$PQUOTA_LIMIT"
log_must check_quota "defaultprojectobjquota" $snap_fs "$PQUOTA_OBJLIMIT"

log_pass "Check the snapshot's defaultproject{obj}quota"
