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
#       Check the zfs groupspace with all parameters
#
#
# STRATEGY:
#       1. set zfs groupquota to a fs
#       2. write some data to the fs with specified user and group
#	3. use zfs groupspace with all possible parameters to check the result
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the zfs groupspace with all possible parameters"

set -A params -- "-n" "-H" "-p" "-o type,name,used,quota" \
    "-o name,used,quota" "-o used,quota" "-o used" "-o quota" "-s type" \
    "-s name" "-s used" "-s quota" "-S type" "-S name" "-S used" "-S quota" \
    "-t posixuser" "-t posixgroup" "-t all" "-i" "-t smbuser" "-t smbgroup"

typeset snap_fs=$QFS@snap

log_must zfs set groupquota@$QGROUP=500m $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 50m $QFILE

sync

log_must zfs snapshot $snap_fs

for param in "${params[@]}"; do
	log_must eval "zfs groupspace $param $QFS >/dev/null 2>&1"
	log_must eval "zfs groupspace $param $snap_fs >/dev/null 2>&1"
done

log_pass "Check the zfs groupspace with all possible parameters"
