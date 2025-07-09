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
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
#	Verify 'zpool trim -a' works correctly with multiple pools
#
# STRATEGY:
#	1. Create multiple pools.
#	2. Start a trim on all pools using 'zpool trim -a'.
#	3. Verify that the trim is started on all pools.
#	4. Wait for the trim to complete.
#	5. Verify that the trim is complete on all pools.
#	6. Start a trim on all pools using 'zpool trim -w -a'.
#	7. Verify that the trim is complete on all pools.
#	8. Now test the -c and -s options on multiple pools with -a.
#	9. Verify that the trim status is correct for each option.
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

log_assert "Verify if trim '-a' works on multiple pools correctly."

DEVSIZE='5G'
TESTDIR="$TEST_BASE_DIR/zpool_trim_multiple_pools"
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

# Start a trim on all pools using 'zpool trim -a'.
log_must zpool trim -a

# Verify that the trim is started on all pools.
for pool in {1..4}; do
    [[ -z "$(trim_progress $TESTPOOL${pool} ${DISK[$pool]})" ]] && \
    log_fail "Trim did not start on pool $TESTPOOL${pool}"
done

# Wait for the trim to complete on all pools.
for pool in {1..4}; do
	log_must zpool wait -t trim $TESTPOOL${pool}
done

# Verify that the trim status is complete on all pools.
complete_count=$(zpool status -t | grep -c "completed")
if [[ $complete_count -ne 4 ]]; then
	log_fail "Expected 4 pools to have trim status 'completed', but found ${complete_count}."
fi

# Start a trim on all pools using 'zpool trim -w -a'
log_must zpool trim -w -a

# Verify that the trim status is complete on all pools.
complete_count=$(zpool status -t | grep -c "completed")
if [[ $complete_count -ne 4 ]]; then
	log_fail "Expected 4 pools to have trim status 'completed', but found ${complete_count}."
fi

# Now test the -s and -c options on multiple pools with -a.
log_must zpool trim -r 1 -a

for pool in {1..4}; do
	[[ -z "$(trim_progress $TESTPOOL${pool} ${DISK[$pool]})" ]] && \
        log_fail "Trim did not start"
done

log_must zpool trim -a -s
complete_count=$(zpool status -t | grep -c "suspended")
if [[ $complete_count -ne 4 ]]; then
	log_fail "Expected 4 pools to have trim status 'suspended', but found $complete_count."
fi

log_must zpool trim -a -c
for pool in {1..4}; do
    [[ -z "$(trim_progress $TESTPOOL${pool} ${DISK[$pool]})" ]] || \
    log_fail "TRIM did not stop on pool $TESTPOOL${pool}"
done

log_pass "Trim '-a' works on multiple pools correctly."
