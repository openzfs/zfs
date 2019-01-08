#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS 'snapshot_count' property is handled correctly by various actions
#
# STRATEGY:
# 1. Verify 'zfs snapshot' increments 'snapshot_count' value
# 2. Verify 'zfs destroy' decrements the value
# 3. Verify 'zfs rename' updates counts across different hierarchies
# 4. Verify 'zfs promote' updates counts across different hierarchies
# 5. Verify 'zfs receive' correct behaviour
#

verify_runnable "both"

function setup
{
	log_must zfs create "$DATASET_TEST"
	log_must zfs create "$DATASET_UTIL"
	# Set snapshot_limit just to activate the snapshot_count property
	log_must zfs set snapshot_limit=100 "$DATASET_TEST"
}

function cleanup
{
	destroy_dataset "$DATASET_TEST" "-Rf"
	destroy_dataset "$DATASET_UTIL" "-Rf"
	rm -f $ZSTREAM
}

log_assert "Verify 'snapshot_count' is handled correctly by various actions"
log_onexit cleanup

DATASET_TEST="$TESTPOOL/$TESTFS/snapshot_count_test"
DATASET_UTIL="$TESTPOOL/$TESTFS/snapshot_count_util"
ZSTREAM="$TEST_BASE_DIR/snapshot_count.$$"

# 1. Verify 'zfs snapshot' increments 'snapshot_count' value
setup
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "0"
log_must zfs snapshot "$DATASET_TEST@snap"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "1"
cleanup

# 2. Verify 'zfs destroy' decrements the value
setup
log_must zfs snapshot "$DATASET_TEST@snap"
log_must zfs destroy "$DATASET_TEST@snap"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "0"
cleanup

# 3. Verify 'zfs rename' updates counts across different hierarchies
setup
log_must zfs create "$DATASET_TEST/renamed"
log_must zfs snapshot "$DATASET_TEST/renamed@snap"
log_must zfs rename "$DATASET_TEST/renamed" "$DATASET_UTIL/renamed"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "0"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "1"
cleanup

# 4. Verify 'zfs promote' updates counts across different hierarchies
setup
log_must zfs create "$DATASET_UTIL/promote"
log_must zfs snapshot "$DATASET_UTIL/promote@snap"
log_must zfs clone "$DATASET_UTIL/promote@snap" "$DATASET_TEST/promoted"
log_must zfs promote "$DATASET_TEST/promoted"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "1"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "0"
cleanup

# 5. Verify 'zfs receive' correct behaviour
setup
log_must zfs create "$DATASET_UTIL/send"
log_must zfs snapshot "$DATASET_UTIL/send@snap1"
log_must zfs snapshot "$DATASET_UTIL/send@snap2"
log_must eval "zfs send $DATASET_UTIL/send@snap1 > $ZSTREAM"
log_must eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "1"
log_must eval "zfs send -i @snap1 $DATASET_UTIL/send@snap2 > $ZSTREAM"
log_must eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "2"

log_pass "'snapshot_count' property is handled correctly"
