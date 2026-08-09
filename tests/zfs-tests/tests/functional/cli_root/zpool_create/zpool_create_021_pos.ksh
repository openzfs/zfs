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
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create -O property=value pool' can successfully create a pool
# with correct filesystem property set.
#
# STRATEGY:
# 1. Create a storage pool with -O option
# 2. Verify the pool created successfully
# 3. Verify the filesystem property is correctly set
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must rm -df "/tmp/mnt$$"
}

log_onexit cleanup

log_assert "'zpool create -O property=value pool' can successfully create a pool" \
		"with correct filesystem property set."

set -A RW_FS_PROP "quota=536870912" \
		  "reservation=536870912" \
		  "recordsize=262144" \
		  "mountpoint=/tmp/mnt$$" \
		  "checksum=fletcher2" \
		  "compression=lzjb" \
		  "atime=off" \
		  "devices=off" \
		  "exec=off" \
		  "setuid=off" \
		  "readonly=on" \
		  "snapdir=visible" \
		  "acltype=posix" \
		  "aclinherit=discard" \
		  "canmount=off"
if is_freebsd; then
	RW_FS_PROP+=("jailed=on")
else
	RW_FS_PROP+=("zoned=on")
fi

typeset -i i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
	log_must zpool create -O ${RW_FS_PROP[$i]} -f $TESTPOOL $DISKS
	log_must datasetexists $TESTPOOL
	log_must propertycheck $TESTPOOL ${RW_FS_PROP[i]}
	log_must zpool destroy $TESTPOOL
	(( i = i + 1 ))
done

log_pass "'zpool create -O property=value pool' can successfully create a pool" \
		"with correct filesystem property set."
