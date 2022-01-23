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
# Copyright (c) 2017 by Fan Yong. Fan rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Check the zfs projectspace with kinds of parameters
#
#
# STRATEGY:
#	1. set zfs projectspace to a fs
#	2. write some data to the fs with specified project ID
#	3. use zfs projectspace with all possible parameters to check the result
#	4. use zfs projectspace with some bad parameters to check the result
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_projectquota
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Check the zfs projectspace with all possible parameters"

set -A good_params -- "-H" "-p" "-o type,name,used,quota" "-o name,used,quota" \
    "-o used,quota" "-o objused" "-o quota" "-s type" "-s name" "-s used" \
    "-s quota" "-S type" "-S name" "-S used" "-S quota"

typeset snap_fs=$QFS@snap

log_must zfs set projectquota@$PRJID1=100m $QFS
log_must zfs set projectobjquota@$PRJID1=100 $QFS
mkmount_writable $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR
log_must user_run $PUSER mkfile 50m $PRJDIR/qf
sync_all_pools

log_must zfs snapshot $snap_fs

for param in "${good_params[@]}"; do
	log_must eval "zfs projectspace $param $QFS >/dev/null 2>&1"
	log_must eval "zfs projectspace $param $snap_fs >/dev/null 2>&1"
done

log_assert "Check the zfs projectspace with some bad parameters"

set -A bad_params -- "-i" "-n" "-P" "-t posixuser"

for param in "${bad_params[@]}"; do
	log_mustnot eval "zfs projectspace $param $QFS >/dev/null 2>&1"
	log_mustnot eval "zfs projectspace $param $snap_fs >/dev/null 2>&1"
done

log_pass "zfs projectspace with kinds of parameters pass"
