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

#
# DESCRIPTION:
# Verify that SIGKILL during multi-threaded ZFS read/write I/O does not
# trigger a kernel panic in the hardened usercopy checks.
#
# When a process is OOM-killed (or otherwise receives SIGKILL) while
# multiple threads are performing ZFS I/O via read(2)/write(2), the
# address space teardown can race with in-flight copy_to_iter() /
# copy_from_iter() calls, potentially triggering:
#   kernel BUG at mm/usercopy.c:102
#
# This test exercises that scenario by running a multi-threaded I/O
# helper and killing it with SIGKILL while I/O is in progress.
#
# STRATEGY:
# 1. Create a test file with data on a ZFS filesystem.
# 2. Launch a multi-threaded I/O process (reads, writes, and mixed).
# 3. Kill it with SIGKILL after a brief delay.
# 4. Repeat several iterations to maximize race window coverage.
# 5. Verify the pool is still healthy.
# 6. Check dmesg for kernel BUG messages.
#
# See openzfs/zfs#15918.
#

verify_runnable "global"

TESTFILE="$TESTDIR/sigkill_test_file"

function cleanup
{
	rm -f "$TESTFILE"
}

log_assert "SIGKILL during multi-threaded ZFS I/O does not cause kernel panic"
log_onexit cleanup

if ! is_mp; then
	log_unsupported "This test requires a multi-processor system."
fi

# Create test file with data for read tests
log_must dd if=/dev/urandom of="$TESTFILE" bs=1M count=32

# Record dmesg marker so we only check new messages
if is_linux; then
	DMESG_LINES=$(dmesg | wc -l)
fi

# Run several iterations of multi-threaded I/O + SIGKILL for each mode:
#   r  = multiple threads reading
#   w  = multiple threads writing
#   rw = mixed read and write threads
for mode in "r" "w" "rw"; do
	for i in $(seq 1 5); do
		sigkill_iothread "$TESTFILE" $mode 4 &
		PID=$!
		log_note "Started sigkill_iothread (mode=$mode, pid=$PID," \
		    "iteration=$i)"

		# Let the threads run I/O briefly
		sleep 0.5

		# Kill with SIGKILL (simulating OOM killer behavior)
		kill -9 $PID 2>/dev/null
		wait $PID 2>/dev/null
	done
done

# Verify pool is still healthy after all the kills
log_must zpool status $TESTPOOL

# Check dmesg for kernel BUG (Linux only)
if is_linux; then
	if dmesg | tail -n +$((DMESG_LINES + 1)) | grep -q "kernel BUG at"; then
		log_fail "Kernel BUG detected in dmesg after SIGKILL during I/O"
	fi
fi

log_pass "SIGKILL during multi-threaded ZFS I/O completed without kernel panic"
