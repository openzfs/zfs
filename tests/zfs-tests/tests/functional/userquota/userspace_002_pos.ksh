#!/bin/ksh -p
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the user used size and quota in zfs userspace
#
#
# STRATEGY:
#       1. set zfs userquota to a fs
#       2. write some data to the fs with specified user and size
#	3. use zfs userspace to check the used size and quota size
#

function cleanup
{
	datasetexists $snapfs && destroy_dataset $snapfs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the zfs userspace used and quota"

log_must zfs set userquota@$QUSER1=100m $QFS

mkmount_writable $QFS

log_must user_run $QUSER1 mkfile 50m $QFILE
sync

typeset snapfs=$QFS@snap

log_must zfs snapshot $snapfs

log_must eval "zfs userspace $QFS >/dev/null 2>&1"
log_must eval "zfs userspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the quota size in zfs userspace $fs"
	log_must eval "zfs userspace $fs | grep $QUSER1 | grep 100M"

	log_note "check the user used size in zfs userspace $fs"
	log_must eval "zfs userspace $fs | grep $QUSER1 | grep 50\\.\*M"
done

log_pass "Check the zfs userspace used and quota"
