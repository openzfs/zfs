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
# Suspending and resuming initializing works.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Start initializing and verify that initializing is active.
# 3. Wait 3 seconds, then suspend initializing and verify that the progress
#    reporting says so.
# 4. Wait 5 seconds and ensure initializing progress doesn't advance.
# 5. Restart initializing and verify that the progress doesn't regress.
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

# Make initializing slow enough that it does not race to completion before it
# can be suspended and observed, rather than finishing on a small or fast vdev
# (see zpool_wait_initialize_*).
default_chunk_sz=$(get_tunable INITIALIZE_CHUNK_SIZE)
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 512

log_must zpool create -f $TESTPOOL $DISK1
log_must zpool initialize $TESTPOOL

[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] && \
    log_fail "Initializing did not start"

sleep 5
log_must zpool initialize -s $TESTPOOL
log_must eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"
progress="$(initialize_progress $TESTPOOL $DISK1)"

sleep 3
[[ "$progress" -eq "$(initialize_progress $TESTPOOL $DISK1)" ]] || \
        log_fail "Initializing progress advanced while suspended"

log_must zpool initialize $TESTPOOL $DISK1
[[ "$progress" -le "$(initialize_progress $TESTPOOL $DISK1)" ]] ||
        log_fail "Initializing progress regressed after resuming"

log_pass "Suspend + resume initializing works as expected"
