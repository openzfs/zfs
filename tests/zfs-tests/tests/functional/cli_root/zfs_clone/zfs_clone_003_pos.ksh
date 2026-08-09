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
# 'zfs clone -o property=value filesystem' can successfully create a ZFS
# clone filesystem with correct property set.
#
# STRATEGY:
# 1. Create a ZFS clone filesystem in the storage pool with -o option
# 2. Verify the filesystem created successfully
# 3. Verify the property is correctly set
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAPFS && destroy_dataset $SNAPFS -Rf
	log_must rm -df "/tmp/mnt$$"
}

log_onexit cleanup


log_assert "'zfs clone -o property=value filesystem' can successfully create" \
	   "a ZFS clone filesystem with correct property set."

log_must zfs snapshot $SNAPFS

typeset -i i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
	log_must zfs clone -o ${RW_FS_PROP[$i]} $SNAPFS $TESTPOOL/$TESTCLONE
	datasetexists $TESTPOOL/$TESTCLONE || \
		log_fail "zfs clone $TESTPOOL/$TESTCLONE fail."
	propertycheck $TESTPOOL/$TESTCLONE ${RW_FS_PROP[i]} || \
		log_fail "${RW_FS_PROP[i]} is failed to set."
	log_must zfs destroy -f $TESTPOOL/$TESTCLONE
	(( i = i + 1 ))
done

log_pass "'zfs clone -o property=value filesystem' can successfully create" \
         "a ZFS clone filesystem with correct property set."
