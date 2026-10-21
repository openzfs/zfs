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
. $STF_SUITE/tests/functional/async/async.cfg
. $STF_SUITE/tests/functional/async/async.kshlib

#
# DESCRIPTION:
#	Verify that suspending a filesystem while an asynchronous Direct I/O
#	read is in flight does not deadlock.
#
# STRATEGY:
#	1. zfsvfs_teardown() takes the teardown lock as a writer and then waits
#	   for the in-flight async read count to drain.  The read completion
#	   updates atime, which wants the same lock for reading, and only
#	   releases its hold on the count afterwards.  While the completion
#	   takes that lock the two wait on each other and neither finishes.
#	2. Set atime=on with relatime=off, so every read is due an atime
#	   update.  That update is what closes the cycle; under the default
#	   relatime it is due far less often and the window is much narrower.
#	3. Inject a ready-stage delay on reads of the file so that a single
#	   O_DIRECT read is still in flight when the rollback begins, rather
#	   than relying on timing.
#	4. Roll back while the read is in flight and require the rollback to
#	   return.
#
# NOTE:	When this test fails it fails by deadlocking.  The rollback and the
#	taskq thread are left in uninterruptible sleep and the filesystem stays
#	suspended, so the pool is unusable for the rest of the run and until
#	the machine is rebooted.  That is the condition under test rather than
#	anything the test can tidy up.  A timeout around the cleanup does not
#	help: the blocked threads never reach a point where a signal can be
#	delivered, so the timeout waits with them.  Cleanup therefore skips
#	every operation that would touch the suspended filesystem, which is
#	what lets the failure be reported at all instead of the run stalling
#	with no result.
# MORE IMPORTANT NODE: This test creates a deadlock before the code
#   change in this commit. Now with z_async_dio_draining added, and stop
#   accepting new async reads when the filesystem is in teardown, and
#   drain, the deadlock is avoided. So this test should pass.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Asynchronous Direct I/O reads are Linux only"
fi

if ! tunable_exists ASYNC_DIO_ENABLED; then
	log_unsupported "The ASYNC_DIO_ENABLED tunable is not available"
fi

if ! fio_ioengine_available "libaio"; then
	log_unsupported "This test requires the fio libaio ioengine"
fi

typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
typeset testfile="$mntpnt/async_teardown"
typeset snap="$TESTPOOL/$TESTFS@async_teardown"

# The injected delay has to outlast the gap between submitting the read and
# starting the rollback, so that the read is certainly still in flight.
typeset -i delay_ms=10000
typeset -i rollback_after=2
typeset -i rollback_wait=45

typeset orig_async=$(get_tunable ASYNC_DIO_ENABLED)
typeset orig_atime=$(get_prop atime $TESTPOOL/$TESTFS)
typeset orig_relatime=$(get_prop relatime $TESTPOOL/$TESTFS)

# Set once the deadlock has been observed, so cleanup knows not to try.
typeset -i wedged=0

function cleanup
{
	if (( wedged == 1 )); then
		#
		# The filesystem is suspended and the threads holding it are in
		# an uninterruptible wait, so every zfs(8) call against it
		# blocks and no signal ends it; a timeout does not help,
		# because the process never reaches a point where it can be
		# killed.  There is nothing here to unwind.  Restore only the
		# module parameter, which is a sysfs write and is unaffected,
		# and let the failure be reported.
		#
		set_tunable32 ASYNC_DIO_ENABLED $orig_async
		log_note "the dataset is suspended; skipping the parts of" \
		    "cleanup that would block on it"
		return
	fi

	zinject -c all > /dev/null 2>&1
	zfs destroy "$snap" > /dev/null 2>&1
	zfs set atime=$orig_atime $TESTPOOL/$TESTFS > /dev/null 2>&1
	zfs set relatime=$orig_relatime $TESTPOOL/$TESTFS > /dev/null 2>&1
	set_tunable32 ASYNC_DIO_ENABLED $orig_async
	rm -f "$testfile"
}

log_assert "Suspending a filesystem with an async Direct I/O read in flight" \
    "does not deadlock"

log_onexit cleanup

log_must set_tunable32 ASYNC_DIO_ENABLED 1
log_must zfs set atime=on $TESTPOOL/$TESTFS
log_must zfs set relatime=off $TESTPOOL/$TESTFS

log_must fio --filename="$testfile" --name=async-teardown-write \
    --rw=write --bs=128K --size=1M --direct=1 --ioengine=libaio \
    --iodepth=1 --fallocate=none --group_reporting
log_must sync

log_must zfs snapshot "$snap"

#
# Delay the read at the ready stage.  Without this the read would usually
# complete before the rollback could start and the test would report success
# without ever having overlapped the two.
#
log_must zinject -E $delay_ms -T read -t data "$testfile"

# The read is expected to be disrupted by the rollback, so its exit status is
# not meaningful here.  Only the rollback's completion is.
fio --filename="$testfile" --name=async-teardown-read \
    --rw=read --bs=4K --size=4K --direct=1 --ioengine=libaio \
    --iodepth=1 --group_reporting > /dev/null 2>&1 &
typeset readpid=$!

sleep $rollback_after

if ! kill -0 $readpid 2> /dev/null; then
	log_fail "the read finished before the rollback began, so the two" \
	    "never overlapped; the injected delay did not take effect"
fi
log_note "read still in flight after ${rollback_after}s, the rollback will" \
    "overlap it"

zfs rollback -r "$snap" &
typeset rbpid=$!

typeset -i waited=0
while (( waited < rollback_wait )); do
	if ! kill -0 $rbpid 2> /dev/null; then
		break
	fi
	sleep 1
	(( waited = waited + 1 ))
done

if kill -0 $rbpid 2> /dev/null; then
	wedged=1
	log_fail "zfs rollback did not return within ${rollback_wait}s with" \
	    "an async Direct I/O read in flight; the teardown drain and the" \
	    "read completion are deadlocked"
fi

log_note "rollback returned after ${waited}s"

wait $readpid 2> /dev/null

log_pass "Suspending a filesystem with an async Direct I/O read in flight" \
    "does not deadlock"
