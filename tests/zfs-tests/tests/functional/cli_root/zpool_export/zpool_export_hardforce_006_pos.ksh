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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# Verify that forced exit initiation can break through lockless way
# if there is a sleeping spa_namespace_lock holder due to a suspended pool.
#
# STRATEGY:
# 1. Create a pool.
# 2. Write some content to check it later.
# 3. Sync.
# 4. Create the situation with zpool reguid sleeping due to suspended pool.
# 5. Forcibly export.
# 6. Verify that zpool reguid is not running anymore.
# 7. Import the pool back.
# 8. Verify that the content written before is the same.
#

verify_runnable "global"

function cleanup
{
	# The test may fail and leave a sleeping spa_namespace_lock holder.
	# Let's unbreak it first.
	zpool export -F $TESTPOOL

	clear_suspension_artifacts $TESTPOOL
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "zpool export -F of a suspended pool with a sleeping spa_namespace_lock holder."
log_onexit cleanup

log_must create_pool $TESTPOOL raidz $FEDISK0 $FEDISK1 $FEDISK2

FS1=fs1
FS2=fs1/fs2

log_must zfs create $TESTPOOL/$FS1
log_must zfs create $TESTPOOL/$FS2

TESTFILE1="/$TESTPOOL/$FS1/file1.dd"
log_must dd if=/dev/urandom of=$TESTFILE1 \
    oflag=sync bs=1M count=10
log_must zpool sync $TESTPOOL
TESTFILE1_CKSUM="$(xxh128digest $TESTFILE1)"

TESTFILE2="/$TESTPOOL/$FS2/file2.dd"
log_must dd if=/dev/urandom of=$TESTFILE2 \
    oflag=sync bs=1M count=10
log_must zpool sync $TESTPOOL
TESTFILE2_CKSUM="$(xxh128digest $TESTFILE2)"

# The test mechanism is based on the fact that zpool reguid does
# txg_wait_synced while holding the spa_namespace_lock.
#
# The technical idea is to use existing zinject features to make I/O
# slow enough so that zpool reguid starts its work and while it waits
# for a txg sync we trigger pool suspension.

# Slow down writing
zinject -q -d $FEDISK0 -D 200:1 -T write $TESTPOOL
zinject -q -d $FEDISK1 -D 200:1 -T write $TESTPOOL
zinject -q -d $FEDISK2 -D 200:1 -T write $TESTPOOL

# Ideally, we should start zpool reguid first and suspend the pool after,
# but both of them require spa_namespace_lock. So, let's do zinject first.
# Prepare troubles for pool suspension:
zinject -d $FEDISK0 -e io -T probe $TESTPOOL
zinject -d $FEDISK0 -e io -T write $TESTPOOL
zinject -d $FEDISK2 -e io -T probe $TESTPOOL
zinject -d $FEDISK2 -e io -T write $TESTPOOL

# Immediately start zpool reguid
zpool reguid $TESTPOOL &
reguid_pid=$!

# Let it go deeper down to the sync wait point, also let the pool find
# itself in trouble. We need to wait longer due to slowdown injections
# are still active.
sleep 20
log_must test "$(kstat_pool $TESTPOOL state)" = "SUSPENDED"

# Check our assumption of where the zpool reguid is.
if is_linux; then
	cat /proc/$reguid_pid/stack
	log_must grep txg_wait_synced /proc/$reguid_pid/stack
fi
if is_freebsd; then
	/usr/bin/procstat kstack $reguid_pid
	log_must eval 'echo "$(/usr/bin/procstat kstack $reguid_pid)" | grep txg_wait_synced'
fi

# Unfortunately, we cannot clear zinject'ions first as they also need
# spa_namespace_lock which is held by zpool reguid. Let's initiate forced
# exit, which is expected to break through lockless way. The actual export
# is not expected to be done due to zinject'ions references, but it should
# make zpool reguid move forward and release the spa_namespace_lock.
zpool export -F $TESTPOOL
log_must test $? -ne 0

# No we can remove all injections
zinject -c all

# And the final call should complete exporting
log_must zpool export -F $TESTPOOL

wait $reguid_pid

log_must zpool import $TESTPOOL
log_must test "$TESTFILE1_CKSUM" = "$(xxh128digest $TESTFILE1)"
log_must test "$TESTFILE2_CKSUM" = "$(xxh128digest $TESTFILE2)"

log_pass "zpool export -F of a suspended pool with a sleeping spa_namespace_lock holder."
