#!/bin/ksh -p
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
#       userquota and groupquota can not be set against snapshot
#
#
# STRATEGY:
#       1. Set userquota on snap and check the zfs get
#       2. Set groupquota on snap and check the zfs get
#

function cleanup
{
	cleanup_quota

	datasetexists $snap_fs && destroy_dataset $snap_fs
}

log_onexit cleanup

typeset snap_fs=$QFS@snap
log_assert "Check  set userquota and groupquota on snapshot"

log_note "Check can not set user|group quota on snapshot"
log_must zfs snapshot $snap_fs

log_mustnot zfs set userquota@$QUSER1=$UQUOTA_SIZE $snap_fs

log_mustnot zfs set groupquota@$QGROUP=$GQUOTA_SIZE $snap_fs

log_pass "Check  set userquota and groupquota on snapshot"
