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
