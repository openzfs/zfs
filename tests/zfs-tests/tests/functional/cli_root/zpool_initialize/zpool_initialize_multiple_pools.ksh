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
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
#	Verify 'zpool initialize -a' works correctly with multiple pools
#
# STRATEGY:
#	1. Create multiple pools.
#	2. Start a initialize operation on all pools using 'zpool initialize -a'.
#	3. Verify that the initializing is active on all pools.
#	4. Wait for the initialize operation to complete.
#	5. Verify that the initialize operation is complete on all pools.
#	6. Start a initializing on all pools using 'zpool initialize -w -a'.
#	7. Verify that the initialize operation is complete on all pools.
#	8. Now test the -u, -c and -s options on multiple pools with -a.
#	9. Verify that the initialize status is correctly updated on all pools.
#

verify_runnable "global"

cleanup() {
    for pool in {1..4}; do
        zpool destroy $TESTPOOL${pool}
        rm -rf $TESTDIR${pool}
    done
    rm -f $DISK1 $DISK2 $DISK3 $DISK4
}

log_onexit cleanup

log_assert "Verify if 'zpool initialize -a' works correctly with multiple pools."

DEVSIZE='5G'
TESTDIR="$TEST_BASE_DIR/zpool_initialize_multiple_pools"
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
    log_must zpool create $TESTPOOL${pool} ${DISK[$pool]}
done
sync_all_pools

# Start an initialize operation on all pools using 'zpool initialize -a'.
log_must zpool initialize -a

# Verify that the initializing is active on all pools.
for pool in {1..4}; do
    if [[ -z "$(initialize_progress $TESTPOOL${pool} ${DISK[$pool]})" ]]; then
        log_fail "Initializing did not start on pool $TESTPOOL${pool}"
    fi
done

# Wait for the initialize operation to complete on all pools.
for pool in {1..4}; do
    log_must zpool wait -t initialize $TESTPOOL${pool}
done

# Verify that the initialize operation is complete on all pools.
complete_count=$(zpool status -i | grep -c "completed")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have initialize status 'completed', but found ${complete_count}."
fi

# Start an initialize operation on all pools using 'zpool initialize -w -a'.
log_must zpool initialize -w -a

# Verify that the initialize operation is complete on all pools.
complete_count=$(zpool status -i | grep -c "completed")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have initialize status 'completed', but found ${complete_count}."
fi

# Now test the -u, -c and -s options on multiple pools with -a.
log_must zpool initialize -u -a
complete_count=$(zpool status -i | grep -c "uninitialized")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have initialize status 'uninitialized', but found ${complete_count}."
fi

log_must zpool initialize -a

for pool in {1..4}; do
    if [[ -z "$(initialize_progress $TESTPOOL${pool} ${DISK[$pool]})" ]]; then
        log_fail "Initializing did not start on pool $TESTPOOL${pool}"
    fi
done

log_must zpool initialize -a -s
complete_count=$(zpool status -i | grep -c "suspended")
if [[ $complete_count -ne 4 ]]; then
    log_fail "Expected 4 pools to have initialize status 'suspended', but found ${complete_count}."
fi

log_must zpool initialize -a -c
for pool in {1..4}; do
    [[ -z "$(initialize_progress $TESTPOOL${pool} ${DISK[$pool]})" ]] || \
    log_fail "Initialize did not stop on pool $TESTPOOL${pool}"
done

log_pass "Initialize '-a' works on multiple pools correctly."
