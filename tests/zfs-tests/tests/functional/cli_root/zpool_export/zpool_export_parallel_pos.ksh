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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2024 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

# test uses 8 vdevs
MAX_NUM=8
DEVICE_DIR=$TEST_BASE_DIR/dev_import-test


#
# DESCRIPTION:
# 	Verify that pool exports can occur in parallel
#
# STRATEGY:
#	1. Create 8 pools
#	2. Inject an export delay using zinject
#	3. Export half of the pools synchronously to baseline sequential cost
#	4. Export the other half asynchronously to demonstrate parallel savings
#	6. Import 4 pools
#	7. Test zpool export -a
#

verify_runnable "global"

#
# override the minimum sized vdevs
#

POOLNAME="test_pool"

function cleanup
{
	zinject -c all

	for i in {0..$(($MAX_NUM - 1))}; do
		poolexists $POOLNAME-$i && destroy_pool $POOLNAME-$i
	done

	[[ -d $DEVICE_DIR ]] && log_must rm -rf $DEVICE_DIR
}

log_assert "Pool exports can occur in parallel"

log_onexit cleanup

[[ ! -d $DEVICE_DIR ]] && log_must mkdir -p $DEVICE_DIR

#
# Create some pools with export delay injectors
#
for i in {0..$(($MAX_NUM - 1))}; do
	log_must truncate -s $MINVDEVSIZE ${DEVICE_DIR}/disk$i
	log_must zpool create $POOLNAME-$i $DEVICE_DIR/disk$i
	log_must zinject -P export -s 8 $POOLNAME-$i
done

#
# Export half of the pools synchronously
#
SECONDS=0
for i in {0..3}; do
	log_must zpool export $POOLNAME-$i
done
sequential_time=$SECONDS
log_note "sequentially exported 4 pools in $sequential_time seconds"

#
# Export half of the pools in parallel
#
SECONDS=0
for i in {4..7}; do
	log_must zpool export $POOLNAME-$i &
done
wait
parallel_time=$SECONDS
log_note "asyncronously exported 4 pools in $parallel_time seconds"

log_must test $parallel_time -lt $(($sequential_time / 3))

#
# import 4 pools with export delay injectors
#
for i in {4..7}; do
	log_must zpool import -d $DEVICE_DIR/disk$i $POOLNAME-$i
	log_must zinject -P export -s 8 $POOLNAME-$i
done

#
# now test zpool export -a
#
SECONDS=0
log_must zpool export -a
parallel_time=$SECONDS
log_note "asyncronously exported 4 pools, using '-a', in $parallel_time seconds"

log_must test $parallel_time -lt $(($sequential_time / 3))

log_pass "Pool exports occur in parallel"
