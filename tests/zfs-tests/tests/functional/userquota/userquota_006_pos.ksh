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
#       Check the invalid parameter of zfs get user|group quota
#
#
# STRATEGY:
#       1. check the invalid zfs get user|group quota to fs
#       2. check the valid zfs get user|group quota to snapshots
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs get user|group quota"
typeset snap_fs=$QFS@snap

log_must zfs snapshot $snap_fs

set -A no_users "mms1234" "ss@#" "root-122" "1234"
for user in "${no_users[@]}"; do
	log_mustnot eval "id $user >/dev/null 2>&1"
	log_must eval "zfs get userquota@$user $QFS >/dev/null 2>&1"
	log_must eval "zfs get userquota@$user $snap_fs >/dev/null 2>&1"
done

set -A no_groups "aidsf@dfsd@" "123223-dsfds#sdfsd" "mss_#ss" "1234"
for group in "${no_groups[@]}"; do
	if is_freebsd; then
		log_mustnot eval "pw groupdel -n $group >/dev/null 2>&1"
	else
		log_mustnot eval "groupdel $group >/dev/null 2>&1"
	fi
	log_must eval "zfs get groupquota@$group $QFS >/dev/null 2>&1"
	log_must eval "zfs get groupquota@$group $snap_fs >/dev/null 2>&1"
done

log_pass "Check the invalid parameter of zfs get user|group quota pass as expect"
