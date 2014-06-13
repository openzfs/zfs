#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
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
	if datasetexists $snap_fs; then
		log_must $ZFS destroy $snap_fs
	fi

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs get user|group quota"
typeset snap_fs=$QFS@snap

log_must $ZFS snapshot $snap_fs

set -A no_users "mms1234" "ss@#" "root-122" "1234"
for user in "${no_users[@]}"; do
	log_mustnot eval "$ID $user >/dev/null 2>&1"
	log_must eval "$ZFS get userquota@$user $QFS >/dev/null 2>&1"
	log_must eval "$ZFS get userquota@$user $snap_fs >/dev/null 2>&1"
done

set -A no_groups "aidsf@dfsd@" "123223-dsfds#sdfsd" "mss_#ss" "1234"
for group in "${no_groups[@]}"; do
	log_mustnot eval "$GROUPDEL $group > /dev/null 2>&1"
	log_must eval "$ZFS get groupquota@$group $QFS >/dev/null 2>&1"
	log_must eval "$ZFS get groupquota@$group $snap_fs >/dev/null 2>&1"
done

log_pass "Check the invalid parameter of zfs get user|group quota pass as expect"
