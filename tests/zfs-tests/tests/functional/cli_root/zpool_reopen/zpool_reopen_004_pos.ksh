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
# Test zpool reopen -n while scrub is running.
# Checks if re-plugged device is NOT resilvered.
#
# STRATEGY:
# 1. Create a pool
# 2. Remove a disk.
# 3. Write test file to pool.
# 4. Execute scrub.
# 5. "Plug back" disk.
# 6. Reopen a pool with an -n flag.
# 7. Check if resilver was deferred.
# 8. Check if trying to put device to offline fails because of no valid
#    replicas.
#
# NOTES:
#	A 125ms delay is added to make sure that the scrub is running while
#	the reopen is invoked.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	# bring back removed disk online for further tests
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
log_must zpool reopen -n $TESTPOOL
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "unavail"
# 3. Write test file to pool.
log_must generate_random_file /$TESTPOOL/data $LARGE_FILE_SIZE
sync_pool $TESTPOOL
# 4. Execute scrub.
# add delay to I/O requests for remaining disk in pool
log_must zinject -d $DISK2 -D125:1 $TESTPOOL
log_must zpool scrub $TESTPOOL
# 5. "Plug back" disk.
insert_disk $REMOVED_DISK $scsi_host
# 6. Reopen a pool with an -n flag.
log_must zpool reopen -n $TESTPOOL
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "online"
# remove delay from disk
log_must zinject -c all
# 7. Check if scrub scan is NOT replaced by resilver.
log_must wait_for_scrub_end $TESTPOOL $MAXTIMEOUT
log_must is_deferred_scan_started $TESTPOOL

# 8. Check if trying to put device to offline fails because of no valid
#    replicas.
log_must wait_for_resilver_end $TESTPOOL $MAXTIMEOUT
log_must zpool offline $TESTPOOL $DISK2

# clean up
log_must zpool destroy $TESTPOOL

log_pass "Zpool reopen test successful"
