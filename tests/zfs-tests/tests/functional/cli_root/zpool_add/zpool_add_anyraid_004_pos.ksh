#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2026, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that a traditional raidz1 vdev can be added to an existing
# anyraidz1:2 pool.
#
# STRATEGY:
# 1. Create a pool with an anyraidz1:2 vdev.
# 2. Add a traditional raidz1 vdev to the pool using -f.
# 3. Verify the pool has both vdev types via zpool status.
# 4. Write data, record checksums.
# 5. Export/import, verify integrity.
# 6. Run scrub, verify no errors.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	delete_sparse_files
}

log_assert "Traditional raidz1 vdev can be added to an anyraidz1:2 pool"
log_onexit cleanup

create_sparse_files "disk" 6 $MINVDEVSIZE2

log_must zpool create $TESTPOOL anyraidz1:2 $disk0 $disk1 $disk2
log_must poolexists $TESTPOOL

log_must zpool add -f $TESTPOOL raidz1 $disk3 $disk4 $disk5

log_must zpool status $TESTPOOL
zpool status $TESTPOOL | grep -q "anyraidz"
if [ $? -ne 0 ]; then
	log_fail "Pool status does not show anyraidz vdev"
fi
zpool status $TESTPOOL | grep -q "raidz1-"
if [ $? -ne 0 ]; then
	log_fail "Pool status does not show traditional raidz1 vdev"
fi

log_must dd if=/dev/urandom of=/$TESTPOOL/testfile1 bs=1M count=32
checksum1=$(xxh128sum /$TESTPOOL/testfile1)

log_must zpool export $TESTPOOL
log_must zpool import -d /var/tmp/testdir/sparse_files $TESTPOOL

checksum1_after=$(xxh128sum /$TESTPOOL/testfile1)
if [ "$checksum1" != "$checksum1_after" ]; then
	log_fail "Checksum mismatch after import: expected=$checksum1 got=$checksum1_after"
fi

log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL
log_mustnot eval "zpool status $TESTPOOL | grep -q 'errors: No known data errors' && false"

log_pass "Traditional raidz1 vdev can be added to an anyraidz1:2 pool"
