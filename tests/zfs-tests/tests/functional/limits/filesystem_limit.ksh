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
# ZFS 'filesystem_limit' is enforced when executing various actions
#
# STRATEGY:
# 1. Verify 'zfs create' and 'zfs clone' cannot exceed the filesystem_limit
# 2. Verify 'zfs rename' cannot move filesystems exceeding the limit
# 3. Verify 'zfs receive' cannot exceed the limit
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

log_assert "Verify 'filesystem_limit' is enforced executing various actions"
log_onexit cleanup

DATASET_TEST="$TESTPOOL/$TESTFS/filesystem_limit_test"
DATASET_UTIL="$TESTPOOL/$TESTFS/filesystem_limit_util"
ZSTREAM="$TEST_BASE_DIR/filesystem_limit.$$"

# 1. Verify 'zfs create' and 'zfs clone' cannot exceed the filesystem_limit
setup
log_must zfs set filesystem_limit=1 "$DATASET_TEST"
log_must zfs create "$DATASET_TEST/create"
log_mustnot zfs create "$DATASET_TEST/create_exceed"
log_mustnot datasetexists "$DATASET_TEST/create_exceed"
log_must zfs set filesystem_limit=2 "$DATASET_TEST"
log_must zfs snapshot "$DATASET_TEST/create@snap"
log_must zfs clone "$DATASET_TEST/create@snap" "$DATASET_TEST/clone"
log_mustnot zfs clone "$DATASET_TEST/create@snap" "$DATASET_TEST/clone_exceed"
log_mustnot datasetexists "$DATASET_TEST/clone_exceed"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "2"
cleanup

# 2. Verify 'zfs rename' cannot move filesystems exceeding the limit
setup
log_must zfs set filesystem_limit=0 "$DATASET_UTIL"
log_must zfs create "$DATASET_TEST/rename"
log_mustnot zfs rename "$DATASET_TEST/rename" "$DATASET_UTIL/renamed"
log_mustnot datasetexists "$DATASET_UTIL/renamed"
log_must test "$(get_prop 'filesystem_count' "$DATASET_UTIL")" == "0"
cleanup

# 3. Verify 'zfs receive' cannot exceed the limit
setup
log_must zfs set filesystem_limit=0 "$DATASET_TEST"
log_must zfs create "$DATASET_UTIL/send"
log_must zfs snapshot "$DATASET_UTIL/send@snap1"
log_must eval "zfs send $DATASET_UTIL/send@snap1 > $ZSTREAM"
log_mustnot eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_mustnot datasetexists "$DATASET_TEST/received"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "0"

log_pass "'filesystem_limit' property is enforced"
