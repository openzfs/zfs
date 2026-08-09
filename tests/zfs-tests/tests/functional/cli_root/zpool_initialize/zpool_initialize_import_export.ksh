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
# Copyright (c) 2016 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Initializing automatically resumes across import/export.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Start initializing and verify that initializing is active.
# 3. Export the pool.
# 4. Import the pool.
# 5. Verify that initializing resumes and progress does not regress.
# 6. Suspend initializing.
# 7. Repeat steps 3-4.
# 8. Verify that progress does not regress but initializing is still suspended.
#

DISK1=${DISKS%% *}

function cleanup
{
	# Destroy the pool (stopping the initialize thread) before restoring
	# the chunk size, so the running thread never issues a write larger
	# than the buffer it allocated at the smaller size.
	if poolexists $TESTPOOL; then
		log_must zpool destroy -f $TESTPOOL
	fi
	[[ "$default_chunk_sz" ]] && \
	    log_must set_tunable64 INITIALIZE_CHUNK_SIZE $default_chunk_sz
}
log_onexit cleanup

# Make initializing slow enough that it is still running after the pool has
# been exported and re-imported below, rather than racing to completion on a
# small or fast vdev (see zpool_wait_initialize_*).
default_chunk_sz=$(get_tunable INITIALIZE_CHUNK_SIZE)
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 512

log_must zpool create -f $TESTPOOL $DISK1
log_must zpool initialize $TESTPOOL

sleep 2

progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Initializing did not start"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

new_progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ -z "$new_progress" ]] && log_fail "Initializing did not restart after import"
[[ "$progress" -le "$new_progress" ]] || \
    log_fail "Initializing lost progress after import"
log_mustnot eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

log_must zpool initialize -s $TESTPOOL $DISK1
action_date="$(initialize_prog_line $TESTPOOL $DISK1 | \
    sed 's/.*ed at \(.*\)).*/\1/g')"
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
new_action_date=$(initialize_prog_line $TESTPOOL $DISK1 | \
    sed 's/.*ed at \(.*\)).*/\1/g')
[[ "$action_date" != "$new_action_date" ]] && \
    log_fail "Initializing action date did not persist across export/import"

[[ "$new_progress" -le "$(initialize_progress $TESTPOOL $DISK1)" ]] || \
        log_fail "Initializing lost progress after import"

log_must eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

log_pass "Initializing retains state as expected across export/import"
