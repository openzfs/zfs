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
#
#      zfs get all <fs> does not print out userquota/groupquota
#
# STRATEGY:
#       1. set userquota and groupquota to a fs
#       2. check zfs get all fs
#

function cleanup
{
	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check zfs get all will not print out user|group quota"

log_must zfs set userquota@$QUSER1=50m $QFS
log_must zfs set groupquota@$QGROUP=100m $QFS

log_mustnot zfs get all $QFS | grep userquota
log_mustnot zfs get all $QFS | grep groupquota

log_pass "zfs get all will not print out user|group quota"
