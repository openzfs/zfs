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
