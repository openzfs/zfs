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
#	Check project{obj}quota to snapshot that:
#	1) can not set project{obj}quota to snapshot directly
#	2) snapshot can inherit the parent fs's project{obj}quota
#	3) the project{obj}quota will not change even the parent quota changed.
#
#
# STRATEGY:
#	1. create a snapshot of a fs
#	2. set the project{obj}quota to snapshot and expect fail
#	3. set project{obj}quota to fs and check the snapshot
#	4. re-set project{obj}quota to fs and check the snapshot's value
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Check the snapshot's project{obj}quota"
typeset snap_fs=$QFS@snap


log_must zfs set projectquota@$PRJID1=$PQUOTA_LIMIT $QFS
log_must check_quota "projectquota@$PRJID1" $QFS "$PQUOTA_LIMIT"

log_must zfs set projectobjquota@$PRJID2=$PQUOTA_OBJLIMIT $QFS
log_must check_quota "projectobjquota@$PRJID2" $QFS "$PQUOTA_OBJLIMIT"

log_must zfs snapshot $snap_fs

log_note "check the snapshot $snap_fs project{obj}quota"
log_must check_quota "projectquota@$PRJID1" $snap_fs "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $snap_fs "$PQUOTA_OBJLIMIT"

log_note  "set project{obj}quota to $snap_fs which will fail"
log_mustnot zfs set projectquota@$PRJID1=100m $snap_fs
log_mustnot zfs set projectobjquota@$PRJID2=100 $snap_fs

log_note "change the parent's project{obj}quota"
log_must zfs set projectquota@$PRJID1=$((PQUOTA_LIMIT * 2)) $QFS
log_must zfs set projectobjquota@$PRJID2=50 $QFS

log_must check_quota "projectquota@$PRJID1" $QFS $((PQUOTA_LIMIT * 2))
log_must check_quota "projectobjquota@$PRJID2" $QFS 50

log_note "check the snapshot $snap_fs project{obj}quota"
log_must check_quota "projectquota@$PRJID1" $snap_fs "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $snap_fs "$PQUOTA_OBJLIMIT"

log_pass "Check the snapshot's project{obj}quota"
