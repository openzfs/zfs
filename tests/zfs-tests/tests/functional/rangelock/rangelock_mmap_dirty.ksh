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

. $STF_SUITE/tests/functional/rangelock/rangelock.kshlib

#
# DESCRIPTION:
#	read() of a file into a writable shared mapping of the same
#	file, at another offset, completes while other threads
#	dirty the mapping and the dirty page limits are low.
#
# STRATEGY:
#	1. Set vm.dirty_bytes to 4M and vm.dirty_background_bytes
#	   to 1M.
#	2. Four threads read from the first 8M of a file into a
#	   shared mapping of the file past 8M.  Two threads store
#	   into each page of the rest of the mapping.  A copy into
#	   a page that writeback has cleaned faults, and the fault
#	   waits in balance_dirty_pages() until writeback, which
#	   runs zfs_putpage() on the same file, catches up.
#	3. Read each reader's kernel stack while it is in read(),
#	   and count the reads seen in balance_dirty_pages().
#	4. Fail if a read or store does not return within 90
#	   seconds, logging the kernel stacks of the stuck threads
#	   and of other blocked tasks.  Fail if no read was seen
#	   throttled.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Needs Linux dirty page throttling"
fi

FS=$TESTPOOL/$TESTFS/mmap

function cleanup
{
	[[ -e $TEST_BASE_DIR/sysctl-vm-dirty_bytes ]] && \
	    log_must rl_restore_vm
	datasetexists $FS && destroy_dataset $FS -r
}

log_assert "read() into a shared mapping of the same file completes" \
    "under dirty page throttling"
log_onexit cleanup

log_must zfs create -o recordsize=128k $FS
log_must rl_save_vm
VM=/proc/sys/vm
log_must eval "echo $((1024 * 1024)) > $VM/dirty_background_bytes"
log_must eval "echo $((4 * 1024 * 1024)) > $VM/dirty_bytes"
rl_run 60 90 mmap $(get_prop mountpoint $FS)/file

log_pass "read() into a shared mapping of the same file completes" \
    "under dirty page throttling"
