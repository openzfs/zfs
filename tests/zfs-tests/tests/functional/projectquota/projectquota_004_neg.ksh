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
#	Check the invalid parameter of zfs set project{obj}quota
#
#
# STRATEGY:
#	1. check the invalid zfs set project{obj}quota to fs
#	2. check the valid zfs set project{obj}quota to snapshots
#

function cleanup
{
	if datasetexists $snap_fs; then
		log_must zfs destroy $snap_fs
	fi

	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs set project{obj}quota"
typeset snap_fs=$QFS@snap

log_must zfs snapshot $snap_fs

set -A no_prjs "mms1234" "ss@#" "root-122" "-1"
for prj in "${no_prjs[@]}"; do
	log_mustnot zfs set projectquota@$prj=100m $QFS
done

log_note "can set all numeric id even if that id does not exist"
log_must zfs set projectquota@12345678=100m $QFS

set -A sizes "100mfsd" "m0.12m" "GGM" "-1234-m" "123m-m"
for size in "${sizes[@]}"; do
	log_note "can not set projectquota with invalid size parameter"
	log_mustnot zfs set projectquota@$PRJID1=$size $QFS
done

log_note "can not set projectquota to snapshot $snap_fs"
log_mustnot zfs set projectquota@$PRJID1=100m $snap_fs

for prj in "${no_prjs[@]}"; do
	log_mustnot zfs set projectobjquota@$prj=100 $QFS
done

log_note "can not set projectobjquota with invalid size parameter"
log_mustnot zfs set projectobjquota@$PRJID2=100msfsd $QFS

log_note "can not set projectobjquota to snapshot $snap_fs"
log_mustnot zfs set projectobjquota@$PRJID2=100m $snap_fs

log_pass "Check the invalid parameter of zfs set project{obj}quota"
