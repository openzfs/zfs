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
# https://opensource.org/license/CDDL-1.0.
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
