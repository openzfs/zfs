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
# with multiple filesystem properties set.
#
# STRATEGY:
# 1. Create a storage pool with multiple -O options
# 2. Verify the pool created successfully
# 3. Verify the properties are correctly set
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must rm -df "/tmp/mnt$$"
}

log_onexit cleanup

log_assert "'zpool create -O property=value pool' can successfully create a pool" \
		"with multiple filesystem properties set."

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

typeset -i i=0
typeset opts=""

while (( $i < ${#RW_FS_PROP[*]} )); do
	opts="$opts -O ${RW_FS_PROP[$i]}"
	(( i = i + 1 ))
done

log_must zpool create $opts -f $TESTPOOL $DISKS
log_must datasetexists $TESTPOOL

i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
	log_must propertycheck $TESTPOOL ${RW_FS_PROP[i]}
	(( i = i + 1 ))
done

log_pass "'zpool create -O property=value pool' can successfully create a pool" \
		"with multiple filesystem properties set."
