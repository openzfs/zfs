#!/bin/ksh -p

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# msync()s of mmap()'ed file should complete quickly during
# background dirty page writebacks by the kernel.
#

function cleanup
{
	log_must eval "echo $saved_vm_dirty_expire_centisecs > /proc/sys/vm/dirty_expire_centisecs"
	log_must eval "echo $saved_vm_dirty_background_ratio > /proc/sys/vm/dirty_background_ratio"
	log_must eval "echo $saved_vm_dirty_writeback_centisecs > /proc/sys/vm/dirty_writeback_centisecs"

	# revert to some sensible defaults if the values we saved
	# were incorrect due to a previous run being interrupted
	if [ $(</proc/sys/vm/dirty_expire_centisecs) -eq 1 ]; then
		log_must eval "echo 3000 > /proc/sys/vm/dirty_expire_centisecs"
	fi

	if [ $(</proc/sys/vm/dirty_background_ratio) -eq 0 ]; then
		log_must eval "echo 10 > /proc/sys/vm/dirty_background_ratio"
	fi

	if [ $(</proc/sys/vm/dirty_writeback_centisecs) -eq 1 ]; then
		log_must eval "echo 500 > /proc/sys/vm/dirty_writeback_centisecs"
	fi
}

if ! is_linux; then
	log_unsupported "Only supported on Linux, requires /proc/sys/vm/ tunables"
fi

log_onexit cleanup
log_assert "Run the tests for mmap_sync"

read -r saved_vm_dirty_expire_centisecs < /proc/sys/vm/dirty_expire_centisecs
read -r saved_vm_dirty_background_ratio < /proc/sys/vm/dirty_background_ratio
read -r saved_vm_dirty_writeback_centisecs < /proc/sys/vm/dirty_writeback_centisecs

log_must eval "echo 1 > /proc/sys/vm/dirty_expire_centisecs"
log_must eval "echo 1 > /proc/sys/vm/dirty_background_bytes"
log_must eval "echo 1 > /proc/sys/vm/dirty_writeback_centisecs"

log_must mmap_sync
log_pass "mmap_sync tests passed."
