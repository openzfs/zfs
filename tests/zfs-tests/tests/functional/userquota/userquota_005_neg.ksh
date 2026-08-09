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
