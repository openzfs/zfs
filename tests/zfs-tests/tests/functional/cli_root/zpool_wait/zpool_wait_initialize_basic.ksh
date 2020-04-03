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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib

#
# DESCRIPTION:
# 'zpool wait' works when waiting for devices to complete initializing
#
# STRATEGY:
# 1. Create a pool.
# 2. Modify a tunable to make sure initializing is slow enough to observe.
# 3. Start initializing the vdev in the pool.
# 4. Start 'zpool wait'.
# 5. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
#

function cleanup
{
	kill_if_running $pid
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

        [[ -d "$TESTDIR" ]] && log_must rm -r "$TESTDIR"

        [[ "$default_chunk_sz" ]] && \
            log_must set_tunable64 INITIALIZE_CHUNK_SIZE $default_chunk_sz
}

typeset -r FILE_VDEV="$TESTDIR/file_vdev"
typeset pid default_chunk_sz

log_onexit cleanup

default_chunk_sz=$(get_tunable INITIALIZE_CHUNK_SIZE)
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 2048

log_must mkdir "$TESTDIR"
log_must mkfile 256M "$FILE_VDEV"
log_must zpool create -f $TESTPOOL "$FILE_VDEV"

log_must zpool initialize $TESTPOOL "$FILE_VDEV"

log_bkgrnd zpool wait -t initialize $TESTPOOL
pid=$!

check_while_waiting $pid "is_vdev_initializing $TESTPOOL"

log_pass "'zpool wait -t initialize' works."
