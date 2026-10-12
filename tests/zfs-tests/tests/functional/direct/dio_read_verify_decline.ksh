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
#	3. Delay every read of the pool's vdevs with zinject, so each read
#	   leaves the data in the caller's buffer for that long before the
#	   checksum is verified.
#	4. On a single file handle, repeatedly read the file with O_DIRECT
#	   while another thread continuously modifies the shared buffer
#	   (manipulate_user_buffer).  The delay makes the very first direct
#	   read fail its checksum verify.
#	5. Confirm at least one dio_verify_rd event was triggered.
#	6. Confirm only the first read was a direct read: the handle declined
#	   Direct I/O after the first benign failure instead of issuing a
#	   direct read (and verify failure) for every request.
#	7. Confirm the pool reports no data errors.
#

verify_runnable "global"

#
# The per-handle Direct I/O read decline is implemented in the Linux zpl layer
# (file->private_data); FreeBSD defines the uio bits but does not drive them,
# so this behaviour is Linux only.
#

NUMBLOCKS=100
BS=$((128 * 1024)) # 128k

#
# Without a delay the buffer is modified between the read completing and its
# checksum verify only by chance, so the number of direct reads before the
# first failure varied from run to run.  Delaying each read by READ_DELAY_MS
# after the data has been copied into the caller's buffer makes the first
# direct read fail its verify, so exactly one direct read is expected.
# Without the decline every one of the NUMBLOCKS reads is a direct read.
#
READ_DELAY_MS=25
MAX_DIRECT_READS=1

function cleanup
{
	zinject -c all
	dio_cleanup
}

log_assert "A benign Direct I/O read verify failure declines Direct I/O " \
    "for the file handle."
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $DIO_VDEVS

create_pool $TESTPOOL1 $DIO_VDEVS
log_must eval "zfs create -o recordsize=128k -o compression=off \
    $TESTPOOL1/$TESTFS1"
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)

log_must stride_dd -o "$mntpnt/direct-read.iso" -i /dev/urandom \
    -b $BS -c $NUMBLOCKS -D

log_must zpool sync $TESTPOOL1
log_must zpool events -c

for vdev in $DIO_VDEVS; do
	log_must zinject -d $vdev -D $READ_DELAY_MS:1 -T read $TESTPOOL1
done

prev_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)

#
# A single file handle, sequential O_DIRECT reads, buffer scribbled by a
# concurrent thread the whole time.
#
log_must manipulate_user_buffer -f "$mntpnt/direct-read.iso" \
    -n $NUMBLOCKS -b $BS -r

log_must zinject -c all

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

# The handle declined Direct I/O after the first benign failure, so only
# that read was a direct read, not all NUMBLOCKS ($NUMBLOCKS).
if [[ $total_dio_rd -gt $MAX_DIRECT_READS ]]; then
	log_fail "Direct I/O was not declined: $total_dio_rd direct reads " \
	    "(> $MAX_DIRECT_READS); the file handle kept issuing direct reads"
fi

log_note "Making sure there are no checksum errors with the ZPool"
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

destroy_pool $TESTPOOL1

log_pass "A benign Direct I/O read verify failure declines Direct I/O " \
    "for the file handle."
