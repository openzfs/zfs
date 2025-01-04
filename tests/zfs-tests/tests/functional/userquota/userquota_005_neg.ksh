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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the invalid parameter of zfs set user|group quota
#
#
# STRATEGY:
#       1. check the invalid zfs set user|group quota to fs
#       1. check the valid zfs set user|group quota to snapshots
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs set user|group quota"
typeset snap_fs=$QFS@snap

log_must zfs snapshot $snap_fs

set -A no_users "mms1234" "ss@#" "root-122"
for user in "${no_users[@]}"; do
	log_mustnot id $user
	log_mustnot zfs set userquota@$user=100m $QFS
done

log_note "can set all numeric id even if that id does not exist"
log_must zfs set userquota@12345678=100m $QFS
log_mustnot zfs set userquota@12345678=100m $snap_fs

set -A sizes "100mfsd" "m0.12m" "GGM" "-1234-m" "123m-m"

for size in "${sizes[@]}"; do
	log_note "can not set user quota with invalid size parameter"
	log_mustnot zfs set userquota@root=$size $QFS
done

log_note "can not set user quota to snapshot $snap_fs"
log_mustnot zfs set userquota@root=100m $snap_fs


set -A no_groups "aidsf@dfsd@" "123223-dsfds#sdfsd" "mss_#ss" "@@@@"
for group in "${no_groups[@]}"; do
	log_mustnot eval "grep $group /etc/group"
	log_mustnot zfs set groupquota@$group=100m $QFS
done

log_note "can not set group quota with invalid size parameter"
log_mustnot zfs set groupquota@root=100msfsd $QFS

log_note "can not set group quota to snapshot $snap_fs"
log_mustnot zfs set groupquota@root=100m $snap_fs

log_pass "Check the invalid parameter of zfs set user|group quota pas as expect"
