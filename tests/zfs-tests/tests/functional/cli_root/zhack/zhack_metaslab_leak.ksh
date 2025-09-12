#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#

#
# Description:
#
# Test whether zhack metaslab leak functions correctly
#
# Strategy:
#
# 1. Create pool on a loopback device with some test data
# 2. Gather pool capacity stats
# 3. Generate fragmentation data with zdb
# 4. Destroy the pool
# 5. Create a new pool with the same configuration
# 6. Export the pool
# 7. Apply the fragmentation information with zhack metaslab leak
# 8. Import the pool
# 9. Verify that pool capacity stats match

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

function cleanup
{
	zpool destroy $TESTPOOL
	rm $tmp
}

log_onexit cleanup
log_assert "zhack metaslab leak leaks the right amount of space"

typeset tmp=$(mktemp)

log_must zpool create $TESTPOOL $DISKS
for i in `seq 1 16`; do
	log_must dd if=/dev/urandom of=/$TESTPOOL/f$i bs=1M count=16
	log_must zpool sync $TESTPOOL
done
for i in `seq 2 2 16`; do
	log_must rm /$TESTPOOL/f$i
done
for i in `seq 1 16`; do
	log_must touch /$TESTPOOL/g$i
	log_must zpool sync $TESTPOOL
done

alloc=$(zpool get -Hpo value alloc $TESTPOOL)
log_must eval "zdb -m --allocated-map $TESTPOOL > $tmp"
log_must zpool destroy $TESTPOOL

log_must zpool create $TESTPOOL $DISKS
log_must zpool export $TESTPOOL
log_must eval "zhack metaslab leak $TESTPOOL < $tmp"
log_must zpool import $TESTPOOL

alloc2=$(zpool get -Hpo value alloc $TESTPOOL)

within_percent $alloc $alloc2 98 || 
	log_fail "space usage changed too much: $alloc to $alloc2"

log_pass "zhack metaslab leak behaved correctly"
