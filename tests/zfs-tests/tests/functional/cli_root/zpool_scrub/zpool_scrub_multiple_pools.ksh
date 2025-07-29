#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2025 Hewlett Packard Enterprise Development LP.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify 'zpool scrub -a' works correctly with multiple pools
#
# STRATEGY:
#	1. Create multiple pools.
#	2. Start a scrub on all pools using 'zpool scrub -a'.
#	3. Verify that the scrub is running on all pools.
#	4. Wait for the scrub to complete.
#	5. Verify that the scrub status is complete on all pools.
#	6. Start a scrub on all pools using 'zpool scrub -w -a'.
#	7. Verify that the scrub status is complete on all pools.
#	8. Now test the -p and -s options on multiple pools with -a.
#	9. Verify that the scrub status is correct for each option.
#

verify_runnable "global"

cleanup() {
    log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
    for pool in {1..4}; do
        zpool destroy $TESTPOOL${pool}
        rm -rf $TESTDIR${pool}
    done
    rm -f $DISK1 $DISK2 $DISK3 $DISK4
    # Import the testpool
    zpool import -a
}

log_onexit cleanup

log_assert "Verify if scrubbing multiple pools works correctly."

# Export the testpool created by setup and Import them later.
log_must zpool export -a

DEVSIZE='128m'
FILESIZE='50m'
TESTDIR="$TEST_BASE_DIR/zpool_scrub_multiple_pools"
DISK1="$TEST_BASE_DIR/zpool_disk1.dat"
DISK2="$TEST_BASE_DIR/zpool_disk2.dat"
DISK3="$TEST_BASE_DIR/zpool_disk3.dat"
DISK4="$TEST_BASE_DIR/zpool_disk4.dat"

truncate -s $DEVSIZE $DISK1
truncate -s $DEVSIZE $DISK2
truncate -s $DEVSIZE $DISK3
truncate -s $DEVSIZE $DISK4

for pool in {1..4}; do
    DISK[$pool]="$TEST_BASE_DIR/zpool_disk${pool}.dat"
    truncate -s $DEVSIZE ${DISK[$pool]}
    log_must zpool create -O mountpoint=$TESTDIR${pool} $TESTPOOL${pool} ${DISK[$pool]}
    log_must zfs create -o compression=off $TESTPOOL${pool}/testfs${pool}
    typeset mntpnt=$(get_prop mountpoint $TESTPOOL${pool}/testfs${pool})
    # Fill some data into the filesystem.
    log_must mkfile $FILESIZE $mntpnt/file${pool}.dat
done
sync_all_pools

# Start a scrub on all pools using 'zpool scrub -a'.
log_must zpool scrub -a
# Wait for the scrub to complete on all pools.
for pool in {1..4}; do
    log_must zpool wait -t scrub $TESTPOOL${pool}
done

# Verify that the scrub status is complete on all pools.
complete_count=$(zpool status -v | grep -c "scrub repaired")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have scrub status 'scrub repaired', but found $complete_count."
fi

# Start a error scrub on all pools using 'zpool scrub -w -a'
log_must zpool scrub -w -a

# Verify that the scrub status is complete on all pools.
complete_count=$(zpool status -v | grep -c "scrub repaired")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have scrub status 'scrub repaired', but found $complete_count."
fi

# Now test the -p and -s options on multiple pools with -a.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool scrub -a
complete_count=$(zpool status -v | grep -c "scrub in progress since")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have scrub status 'scrub in progress since', but found $complete_count."
fi

log_must zpool scrub -a -p
complete_count=$(zpool status -v | grep -c "scrub paused since")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have scrub status 'scrub paused since', but found $complete_count."
fi

log_must zpool scrub -a -s
complete_count=$(zpool status -v | grep -c "scrub canceled")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have scrub status 'scrub canceled', but found $complete_count."
fi

log_pass "Scrubbing multiple pools works correctly."
