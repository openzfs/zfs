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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs create -o property=value filesystem' can successfully create a ZFS
# filesystem with correct property set.
#
# STRATEGY:
# 1. Create a ZFS filesystem in the storage pool with -o option
# 2. Verify the filesystem created successfully
# 3. Verify the property is correctly set
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && destroy_dataset $TESTPOOL/$TESTFS1 -f
}

log_onexit cleanup


log_assert "'zfs create -o property=value filesystem' can successfully create \
	   a ZFS filesystem with correct property set."

typeset -i i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
	log_must zfs create -o ${RW_FS_PROP[$i]} $TESTPOOL/$TESTFS1
	datasetexists $TESTPOOL/$TESTFS1 || \
		log_fail "zfs create $TESTPOOL/$TESTFS1 fail."
	propertycheck $TESTPOOL/$TESTFS1 ${RW_FS_PROP[i]} || \
		log_fail "${RW_FS_PROP[i]} is failed to set."
	log_must_busy zfs destroy -f $TESTPOOL/$TESTFS1
	(( i = i + 1 ))
done

log_pass "'zfs create -o property=value filesystem' can successfully create \
         a ZFS filesystem with correct property set."
