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
. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
# ZFS 'snapshot_limit' is enforced when executing various actions
# NOTE: the limit should *not* be enforced if the user is allowed to change it.
#
# STRATEGY:
# 1. Verify 'zfs snapshot' cannot exceed the snapshot_limit
# 2. Verify 'zfs rename' cannot move snapshots exceeding the limit
# 3. Verify 'zfs promote' cannot exceed the limit
# 4. Verify 'zfs receive' cannot exceed the limit
#

verify_runnable "both"

#
# The has_capability() function was first exported in the 4.10 Linux kernel
# then backported to some LTS kernels.  Prior to this change there was no
# mechanism to perform the needed permission check.  Therefore, this test
# is expected to fail on older kernels and is skipped.
#
if is_linux; then
	if [[ $(linux_version) -lt $(linux_version "4.10") ]]; then
		log_unsupported "Requires has_capability() kernel function"
	fi
fi

function setup
{
	# We can't delegate 'mount' privs under Linux: to avoid issues with
	# commands that may need to (re)mount datasets we set mountpoint=none
	if is_linux; then
		log_must zfs create -o mountpoint=none "$DATASET_TEST"
		log_must zfs create -o mountpoint=none "$DATASET_UTIL"
	else
		log_must zfs create "$DATASET_TEST"
		log_must zfs create "$DATASET_UTIL"
	fi
	if is_freebsd; then
		# Ensure our non-root user has the permission to create the
		# mountpoints and mount the filesystems.
		sysctl vfs.usermount=1
		log_must chmod 777 $(get_prop mountpoint "$DATASET_TEST")
		log_must chmod 777 $(get_prop mountpoint "$DATASET_UTIL")
	fi
	log_must zfs allow -d -l $STAFF1 \
	    'create,snapshot,rename,mount,promote,receive' "$DATASET_TEST"
	log_must zfs allow -d -l $STAFF1 \
	    'create,snapshot,rename,mount,promote,receive' "$DATASET_UTIL"
}

function cleanup
{
	if is_freebsd; then
		sysctl vfs.usermount=0
	fi
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
log_must user_run $STAFF1 zfs snapshot "$DATASET_TEST@snap"
log_mustnot user_run $STAFF1 zfs snapshot "$DATASET_TEST@snap_exceed"
log_mustnot datasetexists "$DATASET_TEST@snap_exceed"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "1"
# Verify snapshot_limit is *not* enforced for users allowed to change it
log_must zfs snapshot "$DATASET_TEST@snap_notenforced_root"
log_must zfs allow -l $STAFF1 'snapshot_limit' "$DATASET_TEST"
log_must user_run $STAFF1 zfs snapshot "$DATASET_TEST@snap_notenforced_user"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "3"
cleanup

# 2. Verify 'zfs rename' cannot move snapshots exceeding the limit
setup
log_must zfs set snapshot_limit=0 "$DATASET_UTIL"
log_must zfs create "$DATASET_TEST/rename"
log_must zfs snapshot "$DATASET_TEST/rename@snap"
log_mustnot user_run $STAFF1 \
    zfs rename "$DATASET_TEST/rename" "$DATASET_UTIL/renamed"
log_mustnot datasetexists "$DATASET_UTIL/renamed"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "0"
# Verify snapshot_limit is *not* enforced for users allowed to change it
log_must zfs rename "$DATASET_TEST/rename" \
    "$DATASET_UTIL/renamed_notenforced_root"
log_must zfs create "$DATASET_TEST/rename"
log_must zfs snapshot "$DATASET_TEST/rename@snap"
log_must zfs allow -l $STAFF1 'snapshot_limit' "$DATASET_UTIL"
log_must user_run $STAFF1 \
    zfs rename "$DATASET_TEST/rename" "$DATASET_UTIL/renamed_notenforced_user"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "2"
cleanup

# 3. Verify 'zfs promote' cannot exceed the limit
setup
log_must zfs set snapshot_limit=0 "$DATASET_UTIL"
log_must zfs create "$DATASET_TEST/promote"
log_must zfs snapshot "$DATASET_TEST/promote@snap"
log_must zfs clone "$DATASET_TEST/promote@snap" "$DATASET_UTIL/promoted"
log_mustnot user_run $STAFF1 zfs promote "$DATASET_UTIL/promoted"
log_mustnot datasetexists "$DATASET_UTIL/promoted@snap"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "0"
# Verify snapshot_limit is *not* enforced for users allowed to change it
log_must zfs promote "$DATASET_UTIL/promoted"
log_must zfs promote "$DATASET_TEST/promote"
log_must zfs allow -l $STAFF1 'snapshot_limit' "$DATASET_UTIL"
log_must user_run $STAFF1 zfs promote "$DATASET_UTIL/promoted"
log_must test "$(get_prop 'snapshot_count' "$DATASET_UTIL")" == "1"
cleanup

# 4. Verify 'zfs receive' cannot exceed the limit
setup
log_must zfs set snapshot_limit=0 "$DATASET_TEST"
log_must zfs create "$DATASET_UTIL/send"
log_must zfs snapshot "$DATASET_UTIL/send@snap1"
log_must eval "zfs send $DATASET_UTIL/send@snap1 > $ZSTREAM"
log_mustnot user_run $STAFF1 \
    eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_mustnot datasetexists "$DATASET_TEST/received"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "0"
log_must zfs set snapshot_limit=1 "$DATASET_TEST"
log_must user_run $STAFF1 \
    eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must zfs snapshot "$DATASET_UTIL/send@snap2"
log_must eval "zfs send -i @snap1 $DATASET_UTIL/send@snap2 > $ZSTREAM"
log_mustnot user_run $STAFF1 \
    eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_mustnot datasetexists "$DATASET_TEST/received@snap2"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "1"
# Verify snapshot_limit is *not* enforced for users allowed to change it
log_must eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must zfs snapshot "$DATASET_UTIL/send@snap3"
log_must eval "zfs send -i @snap2 $DATASET_UTIL/send@snap3 > $ZSTREAM"
log_must zfs allow -l $STAFF1 'snapshot_limit' "$DATASET_TEST"
log_must user_run $STAFF1 \
    eval "zfs receive $DATASET_TEST/received < $ZSTREAM"
log_must test "$(get_prop 'snapshot_count' "$DATASET_TEST")" == "3"

log_pass "'snapshot_limit' property is enforced"
