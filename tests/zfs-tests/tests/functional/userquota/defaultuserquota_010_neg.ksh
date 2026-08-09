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
#       defaultuserquota and defaultgroupquota can not be set against snapshot
#
#
# STRATEGY:
#       1. Set defaultuserquota on snap
#       2. Set defaultgroupquota on snap
#

function cleanup
{
	cleanup_quota

	datasetexists $snap_fs && destroy_dataset $snap_fs
}

log_onexit cleanup

typeset snap_fs=$QFS@snap
log_assert "Check setting default{user|group}quota on snapshot"

log_note "Check can not set default{user|group}quota on snapshot"
log_must zfs snapshot $snap_fs

log_mustnot zfs set defaultuserquota=$UQUOTA_SIZE $snap_fs

log_mustnot zfs set defaultgroupquota=$GQUOTA_SIZE $snap_fs

log_pass "Check setting default{user|group}quota on snapshot fails as expected"
