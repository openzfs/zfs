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
#       Check the zfs userspace with all parameters
#
#
# STRATEGY:
#       1. set zfs userspace to a fs
#       2. write some data to the fs with specified user
#	3. use zfs userspace with all possible parameters to check the result
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the zfs userspace with all possible parameters"

set -A params -- "-n" "-H" "-p" "-o type,name,used,quota" \
    "-o name,used,quota" "-o used,quota" "-o used" "-o quota" "-s type" \
    "-s name" "-s used" "-s quota" "-S type" "-S name" "-S used" "-S quota" \
    "-t posixuser" "-t posixgroup" "-t all" "-i" "-tsmbuser" "-t smbgroup"

typeset snap_fs=$QFS@snap

log_must zfs set userquota@$QUSER1=100m $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 50m $QFILE
sync_all_pools

log_must zfs snapshot $snap_fs

for param in "${params[@]}"; do
	log_must eval "zfs userspace $param $QFS >/dev/null 2>&1"
	log_must eval "zfs userspace $param $snap_fs >/dev/null 2>&1"
done

log_pass "zfs userspace with all possible parameters pass as expect"
