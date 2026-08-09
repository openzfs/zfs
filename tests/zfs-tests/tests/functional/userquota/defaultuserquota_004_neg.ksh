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
