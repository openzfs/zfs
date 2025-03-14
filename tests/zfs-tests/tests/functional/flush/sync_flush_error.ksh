#!/bin/ksh -p
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

#
# This tests that ZFS can correctly detect and correct for a device that fails
# between accepting a write into its cache and writing that cache out to disk.
# This can occur if a backplane or IO module fails and resets the drive before
# its cached can be flushed, or if an active controller between the system and
# the disk is accepting writes for the failed disk.
#
# This presents to ZFS as a write succeeding, with the followup flush failing.
# We simulate this by injecting a "no-op" for the write, which makes it succeed
# without do anything, EIO for the flush, which will be propagated to the write
# and cause its error recovery procedure to execute. That involves probing the
# device, so to maintain the fiction that it has failed, we inject a probe
# failure as well. The probe failure propagates back into the write, and can't
# be recovered from, so the IO is set aside and the pool suspended.
#
# Then, we clear the injections and clear the pool errors. Upon resumption, ZFS
# reexecutes the failed write IO, which completes correctly, and thus the data
# is properly written to disk. We prove this by exporting and importing the
# pool to clear the caches, and verify the file's checksum.
#
# Traditionally, ZFS did not respond to flush errors, so in this same test, the
# write would appear to succeed, the flush error would not be seen, and so the
# probe would never be issued and the pool would not be suspended. Reading the
# data is serviced by the ARC, so it appears to be written, but upon export and
# import, the ARC is cleared, forcing a read from disk. The real file data will
# not have been written, and the block pointer in the dnode will not have been
# updated, so the read fails entirely - the data is lost.
#
# This test tests both sequences by waiting for the pool suspend, but not
# requiring that it does.
#

. $STF_SUITE/include/libtest.shlib

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}

VDEV_FILE="$TMPDIR/vdev_file"

DATA_FILE="$TMPDIR/data_file"

verify_runnable "global"

typeset dev=""

function cleanup
{
	zinject -c all
	zpool clear $TESTPOOL
	destroy_pool $TESTPOOL
	losetup -d $dev || true
	rm -f $VDEV_FILE
}

log_onexit cleanup

log_assert "verify that resuming after a flush fail really writes the data out"

# create 128K of random data, and take its checksum. we do this up front to
# ensure we don't get messed up by any latency from reading /dev/random or
# checksumming the file on the pool
log_must dd if=/dev/random of=$DATA_FILE bs=128K count=1
typeset sum=$(xxh128digest $DATA_FILE)

# create a loop device for the pool
truncate -s 100M $VDEV_FILE
dev=$(basename $(losetup -f))
log_must losetup /dev/$dev $VDEV_FILE

# create a simple pool
log_must zpool create -f $TESTPOOL $dev

# create a file before we start injecting faults. when we reimport the pool
# after the fault we still want the file to exist, otherwise its much harder
# to tell that there's a problem.
log_must dd if=/dev/random of=/$TESTPOOL/data_file bs=128k count=1
log_must zpool sync

# arrange for writes to appear to succeed, but flushes to fail, and the
# followup probe to also fail, causing the pool to suspend
log_note "inject main device faults"
log_must zinject -d $dev -e noop -T write $TESTPOOL
log_must zinject -d $dev -e io -T flush $TESTPOOL
log_must zinject -d $dev -e nxio -T probe $TESTPOOL

# now write to the file
log_must dd if=$DATA_FILE of=/$TESTPOOL/data_file bs=128k count=1

# wait for the pool to suspend. this should happen within ~5s, when the
# txg sync tries to write the change to the pool. we wait 10 seconds to
# give it a chance, but we don't require it, as we're not actually testing
# for suspension.
log_note "waiting for pool to suspend"
typeset -i wait=10
until [[ $wait -eq 0 ]] ; do
	if [[ $(cat /proc/spl/kstat/zfs/$TESTPOOL/state) == "SUSPENDED" ]] ; then
		wait=0
	else
		((wait--))
		sleep 1
	fi
done

# clear injections and resume the pool
log_must zinject -c all
log_must zpool clear $TESTPOOL

# at the this point, regardless of whether or not the pool suspended, the
# txg has passed, with the writes apparently succeeding. if the flush error
# was properly handled, then the write would have been reexecuted and is now
# on disk. if not, then ZFS believes the write is on disk when it is not.

# read the file back. this read should come from the ARC, and so confirm that
# ZFS believes the file has the contents as written
typeset rsum=$(xxh128digest /$TESTPOOL/data_file)
log_must test "$sum" = "$rsum"

# force some txgs out, just to really ensure ZFS thinks everything written
log_must zpool sync -f
log_must zpool sync -f
log_must zpool sync -f

# and check the file again. this should still be coming from the ARC
rsum=$(xxh128digest /$TESTPOOL/data_file)
log_must test "$sum" = "$rsum"

# export and reimport the pool. this will force the ARC to be ejected, so
# the next write will come from the disk
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

# checksum the file again. this time, it's been read back from the disk,
# which was never actually written, so should fail
rsum=$(xxh128digest /$TESTPOOL/data_file)
log_must test "$sum" = "$rsum"

log_pass "verify that resuming after a flush fail really writes the data out"
