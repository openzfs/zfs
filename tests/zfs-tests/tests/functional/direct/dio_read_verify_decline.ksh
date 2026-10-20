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
#	3. On a single file handle, repeatedly read the file with O_DIRECT
#	   while another thread continuously modifies the shared buffer
#	   (manipulate_user_buffer), forcing read checksum verify failures.
#	4. Confirm at least one dio_verify_rd event was triggered.
#	5. Read the file again through the same open file description and
#	   confirm that no further direct reads are issued.
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

function cleanup
{
	exec 3<&-
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

#
# Keep the open file description alive across both helper invocations.
# The first verify failure can happen late in the read, so only measure
# the decline after confirming that it has actually been triggered.
#
exec 3< "$mntpnt/direct-read.iso" || log_fail "Could not open the test file"
prev_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
log_must manipulate_user_buffer -F 3 \
    -n $NUMBLOCKS -b $BS -r 3<&3

curr_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
total_dio_rd=$((curr_dio_rd - prev_dio_rd))
dio_verify=$(get_zed_dio_verify_events $TESTPOOL1 "rd")

log_note "Direct I/O reads: $total_dio_rd, dio_verify_rd events: $dio_verify"

# The benign failure must actually have been triggered, otherwise the
# subsequent buffered reads do not test the decline.
if [[ $dio_verify -lt 1 ]]; then
	log_fail "Expected the benign Direct I/O read verify failure to be " \
	    "triggered, saw $dio_verify dio_verify_rd events"
fi

prev_dio_rd=$curr_dio_rd
log_must manipulate_user_buffer -F 3 -n $NUMBLOCKS -b $BS -r 3<&3
curr_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
total_dio_rd=$((curr_dio_rd - prev_dio_rd))
log_note "Direct I/O reads after the verify failure: $total_dio_rd"

# The first helper has finished, so no direct reads remain in flight.
if [[ $total_dio_rd -ne 0 ]]; then
	log_fail "Direct I/O was not declined: $total_dio_rd direct reads " \
	    "after the confirmed verify failure on the same file handle"
fi

exec 3<&-

log_note "Making sure there are no checksum errors with the ZPool"
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

destroy_pool $TESTPOOL1

log_pass "A benign Direct I/O read verify failure declines Direct I/O " \
    "for the file handle."
