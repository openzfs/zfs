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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#


. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs clone -o property=value volume' can successfully create a ZFS
# clone volume with multiple properties set.
#
# STRATEGY:
# 1. Create a ZFS clone volume in the storage pool with -o option
# 2. Verify the volume created successfully
# 3. Verify the properties are correctly set
#

verify_runnable "global"

function cleanup
{
	snapexists $SNAPFS1 && destroy_dataset $SNAPFS1 -Rf
}

log_onexit cleanup

log_assert "'zfs clone -o property=value volume' can successfully" \
	   "create a ZFS clone volume with multiple correct properties set."

typeset -i i=0
typeset opts=""

log_must zfs snapshot $SNAPFS1

while (( $i < ${#RW_VOL_CLONE_PROP[*]} )); do
	if [[ ${RW_VOL_CLONE_PROP[$i]} != *"checksum"* ]]; then
		opts="$opts -o ${RW_VOL_CLONE_PROP[$i]}"
	fi
	(( i = i + 1 ))
done

log_must zfs clone $opts $SNAPFS1 $TESTPOOL/$TESTCLONE

i=0
while (( $i < ${#RW_VOL_CLONE_PROP[*]} )); do
	if [[ ${RW_VOL_CLONE_PROP[$i]} != *"checksum"* ]]; then
		propertycheck $TESTPOOL/$TESTCLONE ${RW_VOL_CLONE_PROP[i]} || \
			log_fail "${RW_VOL_CLONE_PROP[i]} is failed to set."
	fi
	(( i = i + 1 ))
done

log_pass "'zfs clone -o property=value volume' can successfully" \
	"create a ZFS clone volume with multiple correct properties set."
