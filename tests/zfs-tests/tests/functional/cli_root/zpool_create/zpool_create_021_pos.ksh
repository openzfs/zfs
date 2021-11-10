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
}

log_onexit cleanup

log_assert "'zpool create -O property=value pool' can successfully create a pool \
		with correct filesystem property set."

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
	datasetexists $TESTPOOL || \
		log_fail "zpool create $TESTPOOL fail."
	propertycheck $TESTPOOL ${RW_FS_PROP[i]} || \
		log_fail "${RW_FS_PROP[i]} is failed to set."
	log_must zpool destroy $TESTPOOL
	(( i = i + 1 ))
done

log_pass "'zpool create -O property=value pool' can successfully create a pool \
		with correct filesystem property set."

