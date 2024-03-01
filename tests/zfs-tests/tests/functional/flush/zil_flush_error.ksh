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
# Copyright (c) 2024, Klara, Inc.
#

#
# This tests that if the ZIL write sequence fails, it corectly falls back and
# waits until the transaction has fully committed before returning.
#
# When this test was written, the ZIL has a flaw - it assumes that if its
# writes succeed, then the data is definitely on disk and available for reply
# if the pool fails. It issues a flush immediately after the write, but does
# not check it is result. If a disk fails after the data has been accepted into
# the disk cache, but before it can be written to permanent storage, then
# fsync() will have returned success even though the data is not stored in the
# ZIL for replay.
#
# If the main pool then fails before the transaction can be written, then data
# is lost, and fsync() returning success was premature.
#
# To prove this, we create a pool with a separate log device. We inject two
# faults:
#
# - ZIL writes appear to succeed, but never make it disk
# - ZIL flushes fail, and return error
#
# We then remove the main pool device, and do a write+fsync. This goes to the
# ZIL, and appears to succeed. When the txg closes, the write will fail, and
# the pool suspends.
#
# Then, we simulate a reboot by copying the content of the pool devices aside.
# We restore the pool devices, bring it back online, and export it - we don't
# need it anymore, but we have to clean up properly. Then we restore the copied
# content and import the pool, in whatever state it was in when it suspended.
#
# Finally, we check the content of the file we wrote to. If it matches what we
# wrote, then the fsync() was correct, and all is well. If it doesn't match,
# then the flaw is present, and the test fails.
#
# We run the test twice: once without the log device injections, one with. The
# first confirms the expected behaviour of the ZIL - when the pool is imported,
# the log is replayed. The second fails as above. When the flaw is corrected,
# both will succeed, and this overall test succeeds.
#

. $STF_SUITE/include/libtest.shlib

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}

BACKUP_MAIN="$TMPDIR/backup_main"
BACKUP_LOG="$TMPDIR/backup_log"

LOOP_LOG="$TMPDIR/loop_log"

DATA_FILE="$TMPDIR/data_file"

verify_runnable "global"

function cleanup
{
	zinject -c all
	destroy_pool $TESTPOOL
	unload_scsi_debug
	rm -f $BACKUP_MAIN $BACKUP_LOG $DATA_FILE
}

log_onexit cleanup

log_assert "verify fsync() waits if the ZIL commit fails"

# create 128K of random data, and take its checksum. we do this up front to
# ensure we don't get messed up by any latency from reading /dev/random or
# checksumming the file on the pool
log_must dd if=/dev/random of=$DATA_FILE bs=128K count=1
typeset sum=$(sha256digest $DATA_FILE)

# create a virtual scsi device with two device nodes. these are backed by the
# same memory. we do this because we need to be able to take the device offline
# properly in order to get the pool to suspend; fault injection on a loop
# device can't do it. once offline, we can use the second node to take a copy
# of its state.
load_scsi_debug 100 1 2 1 '512b'
set -A sd $(get_debug_device 2)

# create a loop device for the log.
truncate -s 100M $LOOP_LOG
typeset ld=$(basename $(losetup -f))
log_must losetup /dev/$ld $LOOP_LOG

# this function runs the entire test sequence. the option decides if faults
# are injected on the slog device, mimicking the trigger situation that causes
# the fsync() bug to occur
function test_fsync
{
	typeset -i do_fault_log="$1"

	log_note "setting up test"

	# create the pool. the main data store is on the scsi device, with the
	# log on a loopback. we bias the ZIL towards to the log device to try
	# to ensure that fsync() never involves the main device
	log_must zpool create -f -O logbias=latency $TESTPOOL ${sd[0]} log $ld

	# create the file ahead of time. the ZIL head structure is created on
	# first use, and does a full txg wait, which we need to avoid
	log_must dd if=/dev/zero of=/$TESTPOOL/data_file \
	    bs=128k count=1 conv=fsync
	log_must zpool sync

	# arrange for writes to the log device to appear to succeed, and
	# flushes to fail. this simulates a loss of the device between it
	# accepting the the write into its cache, but before it can be written
	# out
	if [[ $do_fault_log != 0 ]] ; then
		log_note "injecting log device faults"
		log_must zinject -d $ld -e noop -T write $TESTPOOL
		log_must zinject -d $ld -e io -T flush $TESTPOOL
	fi

	# take the main device offline. there is no IO in flight, so ZFS won't
	# notice immediately
	log_note "taking main pool offline"
	log_must eval "echo offline > /sys/block/${sd[0]}/device/state"

	# write out some data, then call fsync(). there are three possible
	# results:
	#
	# - if the bug is present, fsync() will return success, and dd will
	#   succeed "immediately"; before the pool suspends
	# - if the bug is fixed, fsync() will block, the pool will suspend, and
	#   dd will return success after the pool returns to service
	# - if something else goes wrong, dd will fail; this may happen before
	#   or after the pool suspends or returns. this shouldn't happen, and
	#   should abort the test
	#
	# we have to put dd in the background, otherwise if it blocks we will
	# block with it. what we're interested in is whether or not it succeeds
	# before the pool is suspended. if it does, then we expect that after
	# the suspended pool is reimported, the data will have been written
	log_note "running dd in background to write data and call fsync()"
	dd if=$DATA_FILE of=/$TESTPOOL/data_file bs=128k count=1 conv=fsync &
	fsync_pid=$!

	# wait for the pool to suspend. this should happen within ~5s, when the
	# txg sync tries to write the change to the main device
	log_note "waiting for pool to suspend"
	typeset -i tries=10
	until [[ $(cat /proc/spl/kstat/zfs/$TESTPOOL/state) == "SUSPENDED" ]] ; do
		if ((tries-- == 0)); then
			log_fail "pool didn't suspend"
		fi
		sleep 1
	done

	# the pool is suspended. see if dd is still present; if it is, then
	# it's blocked in fsync(), and we have no expectation that the write
	# made it to disk. if dd has exited, then its return code will tell
	# us whether fsync() returned success, or it failed for some other
	# reason
	typeset -i fsync_success=0
	if kill -0 $fsync_pid ; then
		log_note "dd is blocked; fsync() has not returned"
	else
		log_note "dd has finished, ensuring it was successful"
		log_must wait $fsync_pid
		fsync_success=1
	fi

	# pool is suspended. if we online the main device right now, it will
	# retry writing the transaction, which will succed, and everything will
	# continue as its supposed to. that's the opposite of what we want; we
	# want to do an import, as if after reboot, to force the pool to try to
	# replay the ZIL, so we can compare the final result against what
	# fsync() told us
	#
	# however, right now the pool is wedged. we need to get it back online
	# so we can export it, so we can do the import. so we need to copy the
	# entire pool state away. for the scsi device, we can do this through
	# the second device node. for the loopback, we can copy it directly
	log_note "taking copy of suspended pool"
	log_must cp /dev/${sd[1]} $BACKUP_MAIN
	log_must cp /dev/$ld $BACKUP_LOG

	# bring the entire pool back online, by clearing error injections and
	# restoring the main device. this will unblock anything still waiting
	# on it, and tidy up all the internals so we can reset it
	log_note "bringing pool back online"
	if [[ $do_fault_log != 0 ]] ; then
		log_must zinject -c all
	fi
	log_must eval "echo running > /sys/block/${sd[0]}/device/state"
	log_must zpool clear $TESTPOOL

	# now the pool is back online. if dd was blocked, it should now
	# complete successfully. make sure that's true
	if [[ $fsync_success == 0 ]] ; then
		log_note "ensuring blocked dd has now finished"
		log_must wait $fsync_pid
	fi

	log_note "exporting pool"

	# pool now clean, export it
	log_must zpool export $TESTPOOL

	log_note "reverting pool to suspended state"

	# restore the pool to the suspended state, mimicking a reboot
	log_must cp $BACKUP_MAIN /dev/${sd[0]}
	log_must cp $BACKUP_LOG /dev/$ld

	# import the crashed pool
	log_must zpool import $TESTPOOL

	# if fsync() succeeded before the pool suspended, then the ZIL should
	# have replayed properly and the data is now available on the pool
	#
	# note that we don't check the alternative; fsync() blocking does not
	# mean that data _didn't_ make it to disk, just the ZFS never claimed
	# that it did. in that case we can't know what _should_ be on disk
	# right now, so can't check
	if [[ $fsync_success == 1 ]] ; then
		log_note "fsync() succeeded earlier; checking data was written correctly"
		typeset newsum=$(sha256digest /$TESTPOOL/data_file)
		log_must test "$sum" = "$newsum"
	fi

	log_note "test finished, cleaning up"
	log_must zpool destroy -f $TESTPOOL
}

log_note "first run: ZIL succeeds, and repairs the pool at import"
test_fsync 0

log_note "second run: ZIL commit fails, and falls back to txg sync"
test_fsync 1

log_pass "fsync() waits if the ZIL commit fails"
