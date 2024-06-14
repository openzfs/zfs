#!/bin/ksh -p
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

#
# DESCRIPTION:
# 	Verify that pool imports by same name only have one winner
#
# STRATEGY:
#	1. Create 4 single disk pools with the same name
#	2. Generate some ZIL records (for a longer import)
#	3. Export the pools
#	4. Import the pools in parallel
#	5. Repeat with using matching guids
#

verify_runnable "global"

POOLNAME="import_pool"
DEV_DIR_PREFIX="$DEVICE_DIR/$POOLNAME"
VDEVSIZE=$((512 * 1024 * 1024))

log_assert "parallel pool imports by same name only have one winner"

# each pool has its own device directory
for i in {0..3}; do
	log_must mkdir -p ${DEV_DIR_PREFIX}$i
	log_must truncate -s $VDEVSIZE ${DEV_DIR_PREFIX}$i/${DEVICE_FILE}$i
done

function cleanup
{
	zinject -c all
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
	log_must set_tunable64 METASLAB_DEBUG_LOAD 0

	destroy_pool $POOLNAME

	log_must rm -rf $DEV_DIR_PREFIX*
}

log_onexit cleanup

log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
log_must set_tunable64 METASLAB_DEBUG_LOAD 1

function import_pool
{
	typeset dir=$1
	typeset pool=$2
	typeset newname=$3

	SECONDS=0
	errmsg=$(zpool import -N -d $dir -f $pool $newname 2>&1 > /dev/null)
	if [[ $? -eq 0 ]]; then
		touch $dir/imported
		echo "imported $pool in $SECONDS secs"
	elif [[ $errmsg == *"cannot import"* ]]; then
		echo "pool import failed: $errmsg, waited $SECONDS secs"
		touch $dir/failed
	fi
}

#
# create four exported pools with the same name
#
for i in {0..3}; do
	log_must zpool create $POOLNAME ${DEV_DIR_PREFIX}$i/${DEVICE_FILE}$i
	log_must zpool export $POOLNAME
done
log_must zinject -P import -s 10 $POOLNAME

#
# import the pools in parallel, expecting only one winner
#
for i in {0..3}; do
	import_pool ${DEV_DIR_PREFIX}$i $POOLNAME &
done
wait

# check the result of background imports
typeset num_imports=0
typeset num_cannot=0
for i in {0..3}; do
	if [[ -f ${DEV_DIR_PREFIX}$i/imported ]]; then
		((num_imports += 1))
	fi
	if [[ -f ${DEV_DIR_PREFIX}$i/failed ]]; then
		((num_cannot += 1))
		loser=$i
	fi
done
[[ $num_imports -eq "1" ]] || log_fail "expecting an import"
[[ $num_cannot -eq "3" ]] || \
    log_fail "expecting 3 pool exists errors, found $num_cannot"

log_note "$num_imports imported and $num_cannot failed (expected)"

log_pass "parallel pool imports by same name only have one winner"
