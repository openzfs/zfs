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
# ZFS 'snapshot_limit' is enforced when executing various actions
#
# STRATEGY:
# 1. Verify 'zfs snapshot' cannot exceed the snapshot_limit
# 2. Verify 'zfs rename' cannot move snapshots exceeding the limit
# 3. Verify 'zfs promote' cannot exceed the limit
# 4. Verify 'zfs receive' cannot exceed the limit
#

verify_runnable "both"

function setup
{
	log_must zfs create "$DATASET_TEST"
	log_must zfs create "$DATASET_UTIL"
}

function cleanup
{
	destroy_dataset "$DATASET_TEST" "-Rf"
	destroy_dataset "$DATASET_UTIL" "-Rf"
	rm -f $ZSTREAM
}

log_assert "Verify 'snapshot_limit' is enforced when executing various actions"
log_onexit cleanup

DATASET_TEST="$TESTPOOL/$TESTFS/snapshot_limit_test"
DATASET_UTIL="$TESTPOOL/$TESTFS/snapshot_limit_util"
ZSTREAM="$TEST_BASE_DIR/snapshot_limit.$$"

# 1. Verify 'zfs snapshot' cannot exceed the snapshot_limit
setup
log_must zfs set snapshot_limit=1 "$DATASET_TEST"
log_must zfs snapshot "$DATASET_TEST@snap"
log_mustnot zfs snapshot "$DATASET_TEST@snap_exceed"
log_mustnot datasetexists "$DATASET_TEST@snap_exceed"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "1"
cleanup

# 2. Verify 'zfs rename' cannot move snapshots exceeding the limit
setup
log_must zfs set snapshot_limit=0 "$DATASET_UTIL"
log_must zfs create "$DATASET_TEST/rename"
log_must zfs snapshot "$DATASET_TEST/rename@snap"
log_mustnot zfs rename "$DATASET_TEST/rename" "$DATASET_UTIL/renamed"
log_mustnot datasetexists "$DATASET_UTIL/renamed"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "0"
cleanup

# 3. Verify 'zfs promote' cannot exceed the limit
setup
log_must zfs set snapshot_limit=0 "$DATASET_UTIL"
log_must zfs create "$DATASET_TEST/promote"
log_must zfs snapshot "$DATASET_TEST/promote@snap"
log_must zfs clone "$DATASET_TEST/promote@snap" "$DATASET_UTIL/promoted"
log_mustnot zfs promote "$DATASET_UTIL/promoted"
log_mustnot datasetexists "$DATASET_UTIL/promoted@snap"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "0"
cleanup

# 4. Verify 'zfs receive' cannot exceed the limit
setup
log_must zfs set snapshot_limit=0 "$DATASET_TEST"
log_must zfs create "$DATASET_UTIL/send"
log_must zfs snapshot "$DATASET_UTIL/send@snap1"
log_must eval "zfs send $DATASET_UTIL/send@snap1 > $ZSTREAM"
log_mustnot eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_mustnot datasetexists "$DATASET_TEST/received"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "0"
log_must zfs set snapshot_limit=1 "$DATASET_TEST"
log_must eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must zfs snapshot "$DATASET_UTIL/send@snap2"
log_must eval "zfs send -i @snap1 $DATASET_UTIL/send@snap2 > $ZSTREAM"
log_mustnot eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_mustnot datasetexists "$DATASET_TEST/received@snap2"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "1"

log_pass "'snapshot_limit' property is enforced"
