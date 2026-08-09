#!/bin/ksh
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# Create a storage pool with many file based vdevs.
#
# STRATEGY:
# 1. Create assigned number of files in ZFS filesystem as vdevs.
# 2. Creating a new pool based on the vdevs should work.
# 3. Creating a pool with a file based vdev that is too small should fail.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	rm -rf $TESTDIR
}

log_assert "Storage pools with 16 file based vdevs can be created."
log_onexit cleanup

create_pool $TESTPOOL $DISK0
log_must zfs create -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS

vdevs_list=$(echo $TESTDIR/file.{01..16})
log_must truncate -s $MINVDEVSIZE $vdevs_list

create_pool $TESTPOOL1 $vdevs_list
log_must vdevs_in_pool $TESTPOOL1 "$vdevs_list"

if poolexists $TESTPOOL1; then
	destroy_pool $TESTPOOL1
else
	log_fail "Creating pool with large numbers of file-vdevs failed."
fi

log_must mkfile 32m $TESTDIR/broken_file
vdevs_list="$vdevs_list $TESTDIR/broken_file"
log_mustnot zpool create -f $TESTPOOL1 $vdevs_list

log_pass "Storage pools with many file based vdevs can be created."
