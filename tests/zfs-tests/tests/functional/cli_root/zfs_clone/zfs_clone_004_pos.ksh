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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs clone -o property=value filesystem' can successfully create a ZFS
# clone filesystem with multiple properties set.
#
# STRATEGY:
# 1. Create a ZFS clone filesystem in the storage pool with multiple -o options
# 2. Verify the filesystem created successfully
# 3. Verify the properties are correctly set
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAPFS && destroy_dataset $SNAPFS -Rf
}

log_onexit cleanup

log_assert "'zfs clone -o property=value filesystem' can successfully create" \
	   "a ZFS clone filesystem with multiple properties set."

typeset -i i=0
typeset opts=""

log_must zfs snapshot $SNAPFS

while (( $i < ${#RW_FS_PROP[*]} )); do
        if [[ ${RW_FS_PROP[$i]} != *"checksum"* ]]; then
		opts="$opts -o ${RW_FS_PROP[$i]}"
	fi
	(( i = i + 1 ))
done

log_must zfs clone $opts $SNAPFS $TESTPOOL/$TESTCLONE
datasetexists $TESTPOOL/$TESTCLONE || \
	log_fail "zfs create $TESTPOOL/$TESTCLONE fail."

i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
        if [[ ${RW_FS_PROP[$i]} != *"checksum"* ]]; then
		propertycheck $TESTPOOL/$TESTCLONE ${RW_FS_PROP[i]} || \
			log_fail "${RW_FS_PROP[i]} is failed to set."
	fi
	(( i = i + 1 ))
done

log_pass "'zfs clone -o property=value filesystem' can successfully create" \
         "a ZFS clone filesystem with multiple properties set."
