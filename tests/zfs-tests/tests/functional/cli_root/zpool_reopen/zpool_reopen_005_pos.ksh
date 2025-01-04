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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_reopen/zpool_reopen.shlib

#
# DESCRIPTION:
# Test zpool reopen -n while resilver is running.
# Checks if the resilver is restarted.
#
# STRATEGY:
# 1. Create a pool
# 2. Remove a disk.
# 3. Write test file to pool.
# 4. "Plug back" disk.
# 5. Reopen a pool and wait until resilvering is started.
# 6. Reopen a pool again with -n flag.
# 7. Wait until resilvering is finished and check if it was restarted.
#
# NOTES:
#	A 25ms delay is added to make sure that the resilver is running while
#	the reopen is invoked.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	insert_disk $REMOVED_DISK $scsi_host
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Testing zpool reopen with pool name as argument"
log_onexit cleanup

set_removed_disk
scsi_host=$(get_scsi_host $REMOVED_DISK)

# 1. Create a pool
default_mirror_setup_noexit $REMOVED_DISK_ID $DISK2
# 2. Remove a disk.
remove_disk $REMOVED_DISK

log_must zpool reopen $TESTPOOL
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "unavail"
# 3. Write test file to pool.
log_must generate_random_file /$TESTPOOL/data $LARGE_FILE_SIZE
sync_pool $TESTPOOL
# 4. "Plug back" disk.
insert_disk $REMOVED_DISK $scsi_host

# 5. Reopen a pool and wait until resilvering is started.
log_must zpool reopen $TESTPOOL
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "online"
# add delay to I/O requests for the reopened disk
log_must zinject -d $REMOVED_DISK_ID -D25:1 $TESTPOOL
# wait until resilver starts
log_must wait_for_resilver_start $TESTPOOL $MAXTIMEOUT

# 6. Reopen a pool again with -n flag.
log_must zpool reopen -n $TESTPOOL

# 7. Wait until resilvering is finished and check if it was restarted.
log_must wait_for_resilver_end $TESTPOOL $MAXTIMEOUT
# remove delay from disk
log_must zinject -c all
log_mustnot is_scan_restarted $TESTPOOL

# clean up
log_must zpool destroy $TESTPOOL

log_pass "Zpool reopen test successful"
