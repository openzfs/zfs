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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	Verify send streams which contain holes.
#
# STRATEGY:
#	1. Create an initial file for the full send and snapshot.
#	2. Permute the file with holes and snapshot.
#	3. Send the full and incremental snapshots to a new pool.
#	4. Verify the contents of the files match.
#

sendpool=$POOL
sendfs=$sendpool/sendfs

recvpool=$POOL2
recvfs=$recvpool/recvfs

verify_runnable "both"

log_assert "Test hole_birth"
log_onexit cleanup

function cleanup
{
	cleanup_pool $sendpool
	cleanup_pool $recvpool
	set_tunable64 SEND_HOLES_WITHOUT_BIRTH_TIME 1
}

function send_and_verify
{
	log_must eval "zfs send $sendfs@snap1 > $BACKDIR/pool-snap1"
	log_must eval "zfs receive -F $recvfs < $BACKDIR/pool-snap1"

	log_must eval "zfs send -i $sendfs@snap1 $sendfs@snap2 " \
	    ">$BACKDIR/pool-snap1-snap2"
	log_must eval "zfs receive $recvfs < $BACKDIR/pool-snap1-snap2"

	log_must cmp_xxh128 /$sendfs/file1 /$recvfs/file1
}

# By default sending hole_birth times is disabled.  This functionality needs
# to be re-enabled for this test case to verify correctness.  Once we're
# comfortable that all hole_birth bugs has been resolved this behavior may
# be re-enabled by default.
log_must set_tunable64 SEND_HOLES_WITHOUT_BIRTH_TIME 0

# Incremental send truncating the file and adding new data.
log_must zfs create -o recordsize=4k $sendfs

log_must truncate -s 1G /$sendfs/file1
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=4k count=11264 seek=1152
log_must zfs snapshot $sendfs@snap1

log_must truncate -s 4M /$sendfs/file1
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=4k count=152 seek=384 \
    conv=notrunc
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=4k count=10 seek=1408 \
    conv=notrunc
log_must zfs snapshot $sendfs@snap2

send_and_verify
log_must cleanup_pool $sendpool
log_must cleanup_pool $recvpool

# Incremental send appending a hole and data.
log_must zfs create -o recordsize=512 $sendfs

log_must dd if=/dev/urandom of=/$sendfs/file1 bs=128k count=1 seek=1
log_must zfs snapshot $sendfs@snap1

log_must dd if=/dev/urandom of=/$sendfs/file1 bs=128k count=1
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=128k count=1 seek=3
log_must zfs snapshot $sendfs@snap2

send_and_verify
log_must cleanup_pool $sendpool
log_must cleanup_pool $recvpool

# Incremental send truncating the file and adding new data.
log_must zfs create -o recordsize=512 $sendfs

log_must truncate -s 300M /$sendfs/file1
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=512 count=128k conv=notrunc
log_must zfs snapshot $sendfs@snap1

log_must truncate -s 10M /$sendfs/file1
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=512 count=1 seek=96k \
    conv=notrunc
log_must zfs snapshot $sendfs@snap2

send_and_verify

log_pass "Test hole_birth"
