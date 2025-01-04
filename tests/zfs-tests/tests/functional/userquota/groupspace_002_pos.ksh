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
#       Check the user used and groupspace size in zfs groupspace
#
#
# STRATEGY:
#       1. set zfs groupquota to a fs
#       2. write some data to the fs with specified user and size
#	3. use zfs groupspace to check the used size and quota size
#

function cleanup
{
	datasetexists $snapfs && destroy_dataset $snapfs
	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the zfs groupspace used and quota"

log_must zfs set groupquota@$QGROUP=500m $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 100m $QFILE

sync_all_pools

typeset snapfs=$QFS@snap

log_must zfs snapshot $snapfs

log_must eval "zfs groupspace $QFS >/dev/null 2>&1"
log_must eval "zfs groupspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the quota size in zfs groupspace $fs"
	log_must eval "zfs groupspace $fs | grep $QGROUP | grep 500M"

	log_note "check the user used size in zfs groupspace $fs"
	log_must eval "zfs groupspace $fs | grep $QGROUP | grep 100M"
done

log_pass "Check the zfs groupspace used and quota pass as expect"
