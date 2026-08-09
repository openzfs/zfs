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
# Copyright (c) 2025 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Setting vdev scheduler property while reading from vdev should not cause panic.
#
# STRATEGY:
# 1. Create a zpool
# 2. Write a file to the pool.
# 3. Start reading from file, while also setting the scheduler property.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

function set_scheduler
{
	for i in auto on off ; do
		sleep 0.1
		zpool set scheduler=$i $TESTPOOL1 $FILEDEV
	done
}

function cleanup
{
	destroy_pool $TESTPOOL1
	log_must rm -f $FILEDEV
}

log_assert "Toggling vdev scheduler property while reading from vdev should not cause panic"
log_onexit cleanup

# 1. Create a pool

FILEDEV="$TEST_BASE_DIR/filedev.$$"
log_must truncate -s $(($MINVDEVSIZE * 2)) $FILEDEV
create_pool $TESTPOOL1 $FILEDEV

mntpnt=$(get_prop mountpoint $TESTPOOL1)

# 2. Write a file to the pool, while also setting the scheduler property.

log_must eval "fio --filename=$mntpnt/foobar --name=write-file \
		--rw=write --size=$MINVDEVSIZE --bs=128k --numjobs=1 --direct=1 \
		--ioengine=sync --time_based --runtime=2 &"

ITERATIONS=4

for i in $(seq $ITERATIONS); do
	log_must set_scheduler
done;
wait

# 3. Starting reading from file, while also setting the scheduler property.

log_must eval "fio --filename=$mntpnt/foobar --name=read-file \
		--rw=read --size=$MINVDEVSIZE --bs=128k --numjobs=1 --direct=1 \
		--ioengine=sync --time_based --runtime=2 &"

for i in $(seq $ITERATIONS); do
	log_must set_scheduler
done;
wait

log_pass "Setting vdev scheduler property while reading from vdev does not cause panic"
