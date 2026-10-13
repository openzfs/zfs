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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# The zfs-retire agent in the ZED must not leak memory while handling
# a faulted vdev.
#
# STRATEGY:
# 1. Stop the ZED and start it again under valgrind.
# 2. Create a mirrored pool and fault one of its vdevs.
# 3. Wait for the retire agent to finish handling the fault.
# 4. Create a dRAID pool with two failure groups and fault one of its
#    vdevs.  Then fault the vdev at the same index in the other group
#    through I/O errors, so that the ZED diagnoses the fault and the
#    retire agent sees a failure domain failure, both for the fault
#    and for the state change.
# 5. Stop the ZED so valgrind writes its leak report.
# 6. Verify that no definitely lost block was allocated by the retire
#    agent.
#

verify_runnable "global"

if ! command -v valgrind > /dev/null; then
	log_unsupported "valgrind is not installed"
fi

VG_LOG="$TEST_BASE_DIR/zed_retire_leak.valgrind"
VG_ZED_LOG="$TEST_BASE_DIR/zed_retire_leak.zed"
VG_ZED_STATE="$TEST_BASE_DIR/zed_retire_leak.state"
VG_PID=""
DRAID_FILES=$(echo $TEST_BASE_DIR/draid-{0..7})

function stop_vg_zed
{
	typeset -i i=0

	[[ -z "$VG_PID" ]] && return 0
	kill -TERM $VG_PID 2>/dev/null
	# valgrind checks for leaks when the ZED exits, which takes a while.
	while kill -0 $VG_PID 2>/dev/null && ((i < 180)); do
		sleep 1
		((i++))
	done
	if kill -0 $VG_PID 2>/dev/null; then
		kill -KILL $VG_PID 2>/dev/null
		wait $VG_PID 2>/dev/null
		VG_PID=""
		return 1
	fi
	wait $VG_PID 2>/dev/null
	VG_PID=""
	return 0
}

# Wait until the ZED has logged MSG at least COUNT times.
function wait_zed_log # msg count
{
	typeset msg=$1
	typeset -i count=$2
	typeset -i i=0

	while (($(grep -c "$msg" $VG_ZED_LOG) < count)); do
		if ! kill -0 $VG_PID 2>/dev/null; then
			cat $VG_ZED_LOG
			log_fail "ZED exited under valgrind"
		fi
		if ((i >= 300)); then
			cat $VG_ZED_LOG
			log_fail "Timed out waiting for the ZED to log '$msg'"
		fi
		sleep 1
		((i++))
	done
}

function cleanup
{
	stop_vg_zed
	zinject -c all
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL1
	rm -f $VDEV_FILES $DRAID_FILES $VG_LOG $VG_ZED_LOG $VG_ZED_STATE
	# Restart the ZED for the rest of the group.
	if [[ -z "$(zed_check)" ]]; then
		zed_start
	fi
}

log_assert "The ZED retire agent does not leak memory on a vdev fault"
log_onexit cleanup

#
# Run the ZED under valgrind, with the same arguments zed_start uses.
# In a build tree, zed is a libtool wrapper script that execs the real
# binary, so follow exec, but not into the zedlets.  Debian's valgrind
# is a wrapper script that runs "$0.bin", so resolve the symlink to it
# in the constrained path.
#
zed_stop
zed_events_drain
rm -f $VG_LOG $VG_ZED_LOG $VG_ZED_STATE
VALGRIND=$(readlink -f "$(command -v valgrind)")
$VALGRIND --leak-check=full --show-leak-kinds=definite \
    --errors-for-leak-kinds=definite --num-callers=40 \
    --trace-children=yes --trace-children-skip='*.sh' \
    --child-silent-after-fork=yes --log-file=$VG_LOG \
    "$(command -v zed)" -vF -d $ZEDLET_DIR -P $PATH -s $VG_ZED_STATE -j 1 \
    2>$VG_ZED_LOG &
VG_PID=$!
log_note "Started ZED under valgrind, pid $VG_PID"

# Fault a leaf vdev of a mirror.  This posts a statechange event with
# the FAULTED state, which the retire agent handles by looking for a
# hot spare.
truncate -s $MINVDEVSIZE $VDEV_FILES
log_must zpool create -f $TESTPOOL mirror $VDEV_FILES
log_must zpool offline -f $TESTPOOL $FAULT_FILE
log_must check_vdev_state $TESTPOOL $FAULT_FILE "FAULTED"

# The retire agent logs this once it has handled the event.
wait_zed_log "Retire Agent: no spare for" 1

#
# Fault a vdev of a dRAID with two failure groups of four vdevs: not a
# failure domain failure yet, so the retire agent looks for a spare.
#
log_must truncate -s $MINVDEVSIZE $DRAID_FILES
log_must zpool create -f $TESTPOOL1 draid1:2d:4c:8w $DRAID_FILES
log_must zfs create -o primarycache=none -o recordsize=16k $TESTPOOL1/fs
log_must dd if=/dev/urandom of=/$TESTPOOL1/fs/file bs=1M count=32
sync_pool $TESTPOOL1
log_must zpool offline -f $TESTPOOL1 $TEST_BASE_DIR/draid-0
log_must check_vdev_state $TESTPOOL1 $TEST_BASE_DIR/draid-0 "FAULTED"
wait_zed_log "Retire Agent: no spare for" 2

#
# Fail reads from the vdev at the same index in the other group, so the
# ZED diagnoses an I/O fault for it.  The retire agent faults it, and
# then sees that every failure group has lost the vdev at this index:
# once while handling the fault, and once for the state change.
#
log_must zinject -d $TEST_BASE_DIR/draid-4 -e io -T read $TESTPOOL1
log_must cp /$TESTPOOL1/fs/file /dev/null
log_must wait_vdev_state $TESTPOOL1 $TEST_BASE_DIR/draid-4 "FAULTED" 60
wait_zed_log "belongs to draid fdomain failure" 2
log_must zinject -c all

log_must stop_vg_zed
log_must grep -q "LEAK SUMMARY\|All heap blocks were freed" $VG_LOG

#
# Print each definitely lost record that was allocated by the retire
# agent.  If the ZED has no symbols (e.g. a stripped package without its
# debug symbols), its own frames show up as "??? (in .../zed)"; then
# match pool handles leaked from a zpool_iter() call made by the ZED,
# which is how the retire agent looks up pools.
#
LEAKS=$(awk '
	/definitely lost in loss record/ { rec = $0; inrec = 1; next }
	inrec && /^==[0-9]+== *$/ {
		if (rec ~ /zfs_retire|is_draid_fdomain_failure|find_by_guid|remove_spares/ ||
		    rec ~ /zpool_iter[^\n]*\n[^\n]*\?\?\? \(in [^)]*\/zed\)/)
			print rec "\n"
		inrec = 0
		next
	}
	inrec { rec = rec "\n" $0 }
' $VG_LOG)

grep -A6 "LEAK SUMMARY" $VG_LOG
if [[ -n "$LEAKS" ]]; then
	echo "$LEAKS"
	log_fail "The ZED retire agent leaked memory"
fi

log_pass "The ZED retire agent does not leak memory on a vdev fault"
