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
# Verify that a suspended pool with non-forced export hung can be exported
# using hardforce flag.
#
# STRATEGY:
# 1. Create a pool.
# 2. Write some content to check it later.
# 3. Sync.
# 4. Create the situation with non-forced export sleeping as the pool
#    suspension happens after its start.
# 5. Forcibly export.
# 6. Verify that non-forced zpool export is not running.
# 7. Import it back.
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

log_assert "zpool export -F of a suspended pool with non-forced export hung."
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

# The idea is to use existing zinject features to make I/O slow enough
# so that non-forced export starts zfs unmounting process and while
# it waits for sync we trigger pool suspension. Then we can remove
# I/O slowdown and non-forced export is expected to sleep for
# txg_wait_synced while still working on zfs unmount.

# Slow down writing
h1=$(zinject -q -d $FEDISK0 -D 500:1 -T write $TESTPOOL)
h2=$(zinject -q -d $FEDISK1 -D 500:1 -T write $TESTPOOL)
h3=$(zinject -q -d $FEDISK2 -D 500:1 -T write $TESTPOOL)

# Add dirty data, which is not expected to quickly hit disks
# due to slowed I/O
echo hello-world >> /$TESTPOOL/$FS2/hello.txt

# Start non-forced export
zpool export $TESTPOOL &
nonforced_export_pid=$!

# Let it go deeper down to some sync wait logic
sleep 1

# Now we trigger pool suspension
zinject -d $FEDISK0 -e io -T probe $TESTPOOL
zinject -d $FEDISK0 -e io -T write $TESTPOOL
zinject -d $FEDISK2 -e io -T probe $TESTPOOL
zinject -d $FEDISK2 -e io -T write $TESTPOOL

# Slow I/O is not required anymore
zinject -c $h1
zinject -c $h2
zinject -c $h3

# Let it find itself in trouble
sleep 2
log_must test "$(kstat_pool $TESTPOOL state)" = "SUSPENDED"

# Check our assumption of where the non-forced export is.
if is_linux; then
	cat /proc/$nonforced_export_pid/stack
	log_must grep txg_wait_synced /proc/$nonforced_export_pid/stack
fi
if is_freebsd; then
	/usr/bin/procstat kstack $nonforced_export_pid
	log_must eval 'echo "$(/usr/bin/procstat kstack $nonforced_export_pid)" | grep txg_wait_synced'
fi

# No injections required now
zinject -c all

log_must zpool export -F $TESTPOOL

wait $nonforced_export_pid

log_must zpool import $TESTPOOL
log_must test "$TESTFILE1_CKSUM" = "$(xxh128digest $TESTFILE1)"
log_must test "$TESTFILE2_CKSUM" = "$(xxh128digest $TESTFILE2)"

log_pass "zpool export -F of a suspended pool with non-forced export hung."
