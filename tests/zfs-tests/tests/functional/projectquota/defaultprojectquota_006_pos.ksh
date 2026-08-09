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
