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
# Test zpool reopen while scrub is running.
# Checks if re-plugged device is fully resilvered.
#
# STRATEGY:
# 1. Create a pool
# 2. Remove a disk.
# 3. Write a test file to the pool and calculate its checksum.
# 4. Execute scrub.
# 5. "Plug back" disk.
# 6. Reopen a pool.
# 7. Check if scrub scan is replaced by resilver.
# 8. Put another device offline and check if the test file checksum is correct.
#
# NOTES:
#	A 250ms delay is added to make sure that the scrub is running while
#	the reopen kicks the resilver.
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

log_must zpool reopen $TESTPOOL
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "unavail"

# 3. Write a test file to the pool and calculate its checksum.
TESTFILE=/$TESTPOOL/data
log_must generate_random_file /$TESTPOOL/data $LARGE_FILE_SIZE
sync_pool $TESTPOOL
TESTFILE_MD5=$(xxh128digest $TESTFILE)

# 4. Execute scrub.
# add delay to I/O requests for remaining disk in pool
log_must zinject -d $DISK2 -D250:1 $TESTPOOL
log_must zpool scrub $TESTPOOL

# 5. "Plug back" disk.
insert_disk $REMOVED_DISK $scsi_host
# 6. Reopen a pool.
log_must zpool reopen $TESTPOOL
log_must check_state $TESTPOOL "$REMOVED_DISK_ID" "online"
# 7. Check if scrub scan is replaced by resilver.
# the scrub operation has to be running while reopen is executed
log_must is_pool_scrubbing $TESTPOOL true
# remove delay from disk
log_must zinject -c all
# the scrub will be replaced by resilver, wait until it ends
log_must wait_for_resilver_end $TESTPOOL $MAXTIMEOUT
# check if the scrub scan has been interrupted by resilver
log_must is_scan_restarted $TESTPOOL

# 8. Put another device offline and check if the test file checksum is correct.
log_must zpool offline $TESTPOOL $DISK2
CHECK_MD5=$(xxh128digest $TESTFILE)
[[ $CHECK_MD5 == $TESTFILE_MD5 ]] || \
    log_fail "Checksums differ ($CHECK_MD5 != $TESTFILE_MD5)"
log_must zpool online $TESTPOOL $DISK2
sleep 1

# clean up
log_must zpool destroy $TESTPOOL

log_pass "Zpool reopen test successful"
