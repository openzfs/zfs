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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/blkdev.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Initializing a disk with 4096-byte logical sectors works when
# zfs_initialize_chunk_size is smaller than the sector size.
#
# STRATEGY:
# 1. Create a scsi_debug disk with 4096-byte logical and physical sectors.
# 2. Set zfs_initialize_chunk_size to 512 bytes.
# 3. Create a pool on the disk and initialize it to completion.
# 4. Verify that initializing completed without errors.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "scsi_debug module required"
fi

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	restore_tunable INITIALIZE_CHUNK_SIZE
	unload_scsi_debug
}

log_assert "Initializing a 4Kn disk works with a sub-sector chunk size"
log_onexit cleanup

load_scsi_debug $((SPA_MINDEVSIZE / 1024 / 1024 + 64)) 1 1 1 '4Kn'
DISK=$(get_debug_device)

log_must save_tunable INITIALIZE_CHUNK_SIZE
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 512

log_must zpool create -f $TESTPOOL $DISK
log_must zpool initialize -w $TESTPOOL $DISK

state=$(initialize_stat $TESTPOOL $DISK init_state) || \
    log_fail "Could not read initializing state"
errors=$(initialize_stat $TESTPOOL $DISK init_errors) || \
    log_fail "Could not read initializing errors"
[[ "$state" == "COMPLETE" ]] || log_fail "Initializing state is $state"
(( errors == 0 )) || log_fail "Initializing reported $errors errors"

log_pass "Initializing a 4Kn disk works with a sub-sector chunk size"
