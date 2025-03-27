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
#       Check invalid parameter handling of zfs set default{user|group}quota
#
#
# STRATEGY:
#       1. try to set invalid values with zfs set default{user|group}quota to fs
#       2. try to set valid values with zfs set default{user|group}quota to snapshots (an invalid operation)
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check invalid values for zfs set default{user|group}quota"
typeset snap_fs=$QFS@snap

log_must zfs snapshot $snap_fs

set -A sizes "100mfsd" "m0.12m" "GGM" "-1234-m" "123m-m"

for size in "${sizes[@]}"; do
	log_note "can not set default{user|group}quota with invalid size parameter"
	log_mustnot zfs set defaultuserquota=$size $QFS
	log_mustnot zfs set defaultgroupquota=$size $QFS
done

log_note "can not set default{user|group}quota to snapshot $snap_fs"
log_mustnot zfs set defaultuserquota=100m $snap_fs
log_mustnot zfs set defaultgroupquota=100m $snap_fs

log_pass "Check invalid values for zfs set default{user|group}quota passed as expected"
