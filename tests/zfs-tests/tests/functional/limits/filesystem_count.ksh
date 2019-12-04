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
# ZFS 'filesystem_count' property is handled correctly by various actions
#
# STRATEGY:
# 1. Verify 'zfs create' and 'zfs clone' increment 'filesystem_count' value
# 2. Verify 'zfs destroy' decrements the value
# 3. Verify 'zfs rename' updates counts across different hierarchies
# 4. Verify 'zfs promote' preserves counts within different hierarchies
# 5. Verify 'zfs receive' correct behaviour
# 6. Verify 'zfs rollback' does not update 'filesystem_count' value
# 7. Verify 'zfs diff' does not update 'filesystem_count' value
#

verify_runnable "both"

function setup
{
	log_must zfs create "$DATASET_TEST"
	log_must zfs create "$DATASET_UTIL"
	# Set filesystem_limit just to activate the filesystem_count property
	log_must zfs set filesystem_limit=100 "$DATASET_TEST"
}

function cleanup
{
	destroy_dataset "$DATASET_TEST" "-Rf"
	destroy_dataset "$DATASET_UTIL" "-Rf"
	rm -f $ZSTREAM
}

log_assert "Verify 'filesystem_count' is handled correctly by various actions"
log_onexit cleanup

DATASET_TEST="$TESTPOOL/$TESTFS/filesystem_count_test"
DATASET_UTIL="$TESTPOOL/$TESTFS/filesystem_count_util"
ZSTREAM="$TEST_BASE_DIR/filesystem_count.$$"

# 1. Verify 'zfs create' and 'zfs clone' increment 'filesystem_count' value
setup
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "0"
log_must zfs create "$DATASET_TEST/create_clone"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "1"
log_must zfs snapshot "$DATASET_TEST/create_clone@snap"
log_must zfs clone "$DATASET_TEST/create_clone@snap" "$DATASET_TEST/clone"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "2"
cleanup

# 2. Verify 'zfs destroy' decrements the value
setup
log_must zfs create "$DATASET_TEST/destroy"
log_must zfs destroy "$DATASET_TEST/destroy"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "0"
cleanup

# 3. Verify 'zfs rename' updates counts across different hierarchies
setup
log_must zfs create "$DATASET_TEST/rename"
log_must zfs rename "$DATASET_TEST/rename" "$DATASET_UTIL/renamed"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "0"
log_must test "$(get_prop 'filesystem_count' "$DATASET_UTIL")" == "1"
cleanup

# 4. Verify 'zfs promote' preserves counts within different hierarchies
setup
log_must zfs create "$DATASET_UTIL/promote"
log_must zfs snapshot "$DATASET_UTIL/promote@snap"
log_must zfs clone "$DATASET_UTIL/promote@snap" "$DATASET_TEST/promoted"
log_must zfs promote "$DATASET_TEST/promoted"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "1"
log_must test "$(get_prop 'filesystem_count' "$DATASET_UTIL")" == "1"
cleanup

# 5. Verify 'zfs receive' correct behaviour
setup
log_must zfs create "$DATASET_UTIL/send"
log_must zfs snapshot "$DATASET_UTIL/send@snap1"
log_must zfs snapshot "$DATASET_UTIL/send@snap2"
log_must eval "zfs send $DATASET_UTIL/send@snap1 > $ZSTREAM"
log_must eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "1"
log_must eval "zfs send -i @snap1 $DATASET_UTIL/send@snap2 > $ZSTREAM"
log_must eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "1"
cleanup

# 6. Verify 'zfs rollback' does not update 'filesystem_count' value
setup
log_must zfs create "$DATASET_TEST/rollback"
log_must zfs snapshot "$DATASET_TEST/rollback@snap"
log_must zfs rollback "$DATASET_TEST/rollback@snap"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "1"
cleanup

# 7. Verify 'zfs diff' does not update 'filesystem_count' value
setup
log_must zfs create "$DATASET_TEST/diff"
log_must zfs snapshot "$DATASET_TEST/diff@snap1"
log_must zfs snapshot "$DATASET_TEST/diff@snap2"
log_must zfs diff "$DATASET_TEST/diff@snap1" "$DATASET_TEST/diff@snap2"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "1"
log_must zfs diff "$DATASET_TEST/diff@snap2"
log_must test "$(get_prop 'filesystem_count' "$DATASET_TEST")" == "1"

log_pass "'filesystem_count' property is handled correctly"
