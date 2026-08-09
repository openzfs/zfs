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
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	Create a pool with same devices twice or create two pools with same
#	devices, 'zpool create' should failed.
#
# STRATEGY:
#	1. Loop to create the following three kinds of pools.
#		- Regular pool
#		- Mirror
#		- Raidz
#	2. Create two pools but using the same disks, expect failed.
#	3. Create one pool but using the same disks twice, expect failed.
#

verify_runnable "global"

function cleanup
{
	typeset pool

	for pool in $TESTPOOL $TESTPOOL1; do
		poolexists $pool && destroy_pool $pool
	done
}

log_assert "Create a pool with same devices twice or create two pools with " \
	"same devices, 'zpool create' should fail."
log_onexit cleanup

unset NOINUSE_CHECK
typeset opt
for opt in "" "mirror" "raidz" "draid"; do
	if [[ $opt == "" ]]; then
		typeset disks=$DISK0
	else
		typeset disks=$DISKS
	fi

	# Create two pools but using the same disks.
	create_pool $TESTPOOL $opt $disks
	log_mustnot zpool create -f $TESTPOOL1 $opt $disks
	destroy_pool $TESTPOOL

	# Create two pools and part of the devices were overlapped
	create_pool $TESTPOOL $opt $disks
	log_mustnot zpool create -f $TESTPOOL1 $opt $DISK0
	destroy_pool $TESTPOOL

	# Create one pool but using the same disks twice.
	log_mustnot zpool create -f $TESTPOOL $opt $disks $disks
done

log_pass "Using overlapping or in-use disks to create a new pool fails as expected."
