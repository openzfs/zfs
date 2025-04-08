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
# Copyright (c) 2023 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

# test uses 8 vdevs
export MAX_NUM=8

#
# DESCRIPTION:
# 	Verify that pool imports can occur in parallel
#
# STRATEGY:
#	1. Create 8 pools
#	2. Generate some ZIL records
#	3. Export the pools
#	4. Import half of the pools synchronously to baseline sequential cost
#	5. Import the other half asynchronously to demonstrate parallel savings
#	6. Export 4 pools
#	7. Test zpool import -a
#

verify_runnable "global"

#
# override the minimum sized vdevs
#
VDEVSIZE=$((512 * 1024 * 1024))
increase_device_sizes $VDEVSIZE

POOLNAME="import_pool"

function cleanup
{
	zinject -c all
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
	log_must set_tunable64 METASLAB_DEBUG_LOAD 0

	for i in {0..$(($MAX_NUM - 1))}; do
		destroy_pool $POOLNAME-$i
	done
	# reset the devices
	increase_device_sizes 0
	increase_device_sizes $FILE_SIZE
}

log_assert "Pool imports can occur in parallel"

log_onexit cleanup

log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
log_must set_tunable64 METASLAB_DEBUG_LOAD 1


#
# create some exported pools with import delay injectors
#
for i in {0..$(($MAX_NUM - 1))}; do
	log_must zpool create $POOLNAME-$i $DEVICE_DIR/${DEVICE_FILE}$i
	log_must zpool export $POOLNAME-$i
	log_must zinject -P import -s 12 $POOLNAME-$i
done
wait

#
# import half of the pools synchronously
#
SECONDS=0
for i in {0..3}; do
	log_must zpool import -d $DEVICE_DIR -f $POOLNAME-$i
done
sequential_time=$SECONDS
log_note "sequentially imported 4 pools in $sequential_time seconds"

#
# import half of the pools in parallel
#
SECONDS=0
for i in {4..7}; do
	log_must zpool import -d $DEVICE_DIR -f $POOLNAME-$i &
done
wait
parallel_time=$SECONDS
log_note "asyncronously imported 4 pools in $parallel_time seconds"

log_must test $parallel_time -lt $(($sequential_time / 2))

#
# export pools with import delay injectors
#
for i in {4..7}; do
	log_must zpool export $POOLNAME-$i
	log_must zinject -P import -s 12 $POOLNAME-$i
done
wait

#
# now test zpool import -a
#
SECONDS=0
log_must zpool import -a -d $DEVICE_DIR -f
parallel_time=$SECONDS
log_note "asyncronously imported 4 pools in $parallel_time seconds"

log_must test $parallel_time -lt $(($sequential_time / 2))

log_pass "Pool imports occur in parallel"
