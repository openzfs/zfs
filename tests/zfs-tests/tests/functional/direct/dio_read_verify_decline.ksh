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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
#	Verify that a benign Direct I/O read checksum verify failure declines
#	Direct I/O for the offending file handle.
#
#	When an application modifies its O_DIRECT buffer while a read is in
#	flight the post-read checksum verify fails even though the data on
#	disk is good; ZFS discards the direct read, re-reads through the ARC
#	and emits an ereport.fs.zfs.dio_verify_rd.  An application that
#	recycles read buffers across concurrent requests (QEMU's block layer
#	does this) trips the failure on every read, paying the verify and
#	re-read cost repeatedly.  After the first such benign failure on a
#	file handle ZFS should decline Direct I/O for reads on that handle and
#	route them through the buffered path, bounding the cost to the first
#	occurrence while still returning correct data.
#
# STRATEGY:
#	1. Create a pool and a dataset with compression=off.
#	2. Write a file using Direct I/O.
#	3. On a single file handle, repeatedly read the file with O_DIRECT
#	   while another thread continuously modifies the shared buffer
#	   (manipulate_user_buffer), forcing read checksum verify failures.
#	4. Confirm at least one dio_verify_rd event was triggered.
#	5. Confirm the direct read count is bounded to a small number: the
#	   handle declined Direct I/O after the first benign failure instead
#	   of issuing a direct read (and verify failure) for every request.
#	6. Confirm the pool reports no data errors.
#

verify_runnable "global"

#
# The per-handle Direct I/O read decline is implemented in the Linux zpl layer
# (file->private_data); FreeBSD defines the uio bits but does not drive them,
# so this behaviour is Linux only.
#

NUMBLOCKS=500
BS=$((128 * 1024)) # 128k

# With the fix a single direct read hits the benign failure and the handle
# then declines Direct I/O, so the direct read count stays at a small handful.
# Without it every one of the NUMBLOCKS reads is a direct read that fails the
# verify.  Allow a generous margin for the reads issued before the first
# failure is seen.
MAX_DIRECT_READS=10

log_assert "A benign Direct I/O read verify failure declines Direct I/O " \
    "for the file handle."
log_onexit dio_cleanup

log_must truncate -s $MINVDEVSIZE $DIO_VDEVS

create_pool $TESTPOOL1 $DIO_VDEVS
log_must eval "zfs create -o recordsize=128k -o compression=off \
    $TESTPOOL1/$TESTFS1"
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)

log_must stride_dd -o "$mntpnt/direct-read.iso" -i /dev/urandom \
    -b $BS -c $NUMBLOCKS -D

log_must zpool sync $TESTPOOL1
log_must zpool events -c

prev_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)

#
# A single file handle, sequential O_DIRECT reads, buffer scribbled by a
# concurrent thread the whole time.
#
log_must manipulate_user_buffer -f "$mntpnt/direct-read.iso" \
    -n $NUMBLOCKS -b $BS -r

curr_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
total_dio_rd=$((curr_dio_rd - prev_dio_rd))
dio_verify=$(get_zed_dio_verify_events $TESTPOOL1 "rd")

log_note "Direct I/O reads: $total_dio_rd, dio_verify_rd events: $dio_verify"

# The benign failure must actually have been triggered, otherwise the bound
# below is vacuous.
if [[ $dio_verify -lt 1 ]]; then
	log_fail "Expected the benign Direct I/O read verify failure to be " \
	    "triggered, saw $dio_verify dio_verify_rd events"
fi

# The handle declined Direct I/O after the first benign failure, so the
# direct read count is bounded well below NUMBLOCKS ($NUMBLOCKS).
if [[ $total_dio_rd -gt $MAX_DIRECT_READS ]]; then
	log_fail "Direct I/O was not declined: $total_dio_rd direct reads " \
	    "(> $MAX_DIRECT_READS); the file handle kept issuing direct reads"
fi

log_note "Making sure there are no checksum errors with the ZPool"
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

destroy_pool $TESTPOOL1

log_pass "A benign Direct I/O read verify failure declines Direct I/O " \
    "for the file handle."
