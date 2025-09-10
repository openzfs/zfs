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
# Copyright (c) 2025 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Setting vdev io_scheduler property while reading from vdev should not cause panic.
#
# STRATEGY:
# 1. Create a zpool
# 2. Write a file to the pool.
# 3. Start reading from file, while also setting the io_scheduler property.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"
log_must save_tunable DIO_ENABLED
log_must set_tunable32 DIO_ENABLED 1

function set_io_scheduler
{
	zpool set scheduler=none $TESTPOOL1 $FILEDEV
	sleep 0.1
	zpool set scheduler=classic $TESTPOOL1 $FILEDEV
	sleep 0.1
}

function cleanup
{
	destroy_pool $TESTPOOL1
	log_must rm -f $FILEDEV
	log_must restore_tunable DIO_ENABLED
}

log_assert "Toggling vdev scheduler property while reading from vdev should not cause panic"
log_onexit cleanup

# 1. Create a pool

FILEDEV="$TEST_BASE_DIR/filedev.$$"
log_must truncate -s $(($MINVDEVSIZE * 2)) $FILEDEV
create_pool $TESTPOOL1 $FILEDEV

mntpnt=$(get_prop mountpoint $TESTPOOL1)

# 2. Write a file to the pool, while also setting the io_scheduler property.

log_must eval "fio --filename=$mntpnt/foobar --name=write-file \
		--rw=write --size=$MINVDEVSIZE --bs=128k --numjobs=1 --direct=1 \
		--ioengine=sync --time_based --runtime=10 &"

ITERATIONS=30

for i in $(seq $ITERATIONS); do
	log_must set_io_scheduler
done;
wait

# 3. Starting reading from file, while also setting the io_scheduler property.

log_must eval "fio --filename=$mntpnt/foobar --name=read-file \
		--rw=read --size=$MINVDEVSIZE --bs=128k --numjobs=1 --direct=1 \
		--ioengine=sync --time_based --runtime=10 &"

for i in $(seq $ITERATIONS); do
	log_must set_io_scheduler
done;
wait

log_pass "Setting vdev scheduler property while reading from vdev does not cause panic"
