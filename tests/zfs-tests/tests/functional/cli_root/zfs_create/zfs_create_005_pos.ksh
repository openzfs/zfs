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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs create -o property=value filesystem' can successfully create a ZFS
# filesystem with multiple properties set.
#
# STRATEGY:
# 1. Create a ZFS filesystem in the storage pool with multiple -o options
# 2. Verify the filesystem created successfully
# 3. Verify the properties are correctly set
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 &&
		destroy_dataset $TESTPOOL/$TESTFS1 -f
	log_must rm -df "/tmp/mnt$$"
}

log_onexit cleanup


log_assert "'zfs create -o property=value filesystem' can successfully create" \
	   "a ZFS filesystem with multiple properties set."

typeset -i i=0
typeset opts=""

while (( $i < ${#RW_FS_PROP[*]} )); do
        if [[ ${RW_FS_PROP[$i]} != *"checksum"* ]]; then
		opts="$opts -o ${RW_FS_PROP[$i]}"
	fi
	(( i = i + 1 ))
done

log_must zfs create $opts $TESTPOOL/$TESTFS1
log_must datasetexists $TESTPOOL/$TESTFS1

i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
        if [[ ${RW_FS_PROP[$i]} != *"checksum"* ]]; then
		log_must propertycheck $TESTPOOL/$TESTFS1 ${RW_FS_PROP[i]}
	fi
	(( i = i + 1 ))
done

log_pass "'zfs create -o property=value filesystem' can successfully create" \
         "a ZFS filesystem with multiple properties set."
