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
# Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_reopen/zpool_reopen.shlib

#
# DESCRIPTION:
# Test if zpool reopen with no arguments works correctly.
#
# STRATEGY:
# 1. Create a pool.
# 2. Remove a disk.
# 3. Reopen a pool and verify if removed disk is marked as unavailable.
# 4. "Plug back" disk.
# 5. Reopen a pool and verify if removed disk is marked online again.
# 6. Check if reopen caused resilver start.
#

verify_runnable "global"

function cleanup
{
	# bring back removed disk online for further tests
	insert_disk $REMOVED_DISK $scsi_host
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	clear_labels $REMOVED_DISK $DISK2
}

log_assert "Testing zpool reopen with no arguments"
log_onexit cleanup

set_removed_disk
scsi_host=$(get_scsi_host $REMOVED_DISK)

# 1. Create a pool.
default_mirror_setup_noexit $REMOVED_DISK_ID $DISK2
# 2. Remove a disk.
remove_disk $REMOVED_DISK
# 3. Reopen a pool and verify if removed disk is marked as unavailable.
log_must zpool reopen
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "unavail"
# Write some data to the pool
log_must generate_random_file /$TESTPOOL/data $SMALL_FILE_SIZE
# 4. "Plug back" disk.
insert_disk $REMOVED_DISK $scsi_host
# 5. Reopen a pool and verify if removed disk is marked online again.
log_must zpool reopen
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "online"
# 6. Check if reopen caused resilver start.
log_must wait_for_resilver_end $TESTPOOL $MAXTIMEOUT

# clean up
log_must zpool destroy $TESTPOOL
clear_labels $REMOVED_DISK $DISK2

log_pass "Zpool reopen with no arguments test passed"
