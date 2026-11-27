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

. $STF_SUITE/tests/functional/tmpfile_dmlog/tmpfile_dmlog.kshlib

verify_runnable "global"

if ! command -v dmsetup >/dev/null; then
	log_unsupported "dmsetup is not installed"
fi
if ! modprobe dm_log_writes; then
	log_unsupported "the dm_log_writes kernel module is not available"
fi
if ! dmsetup targets | grep -q "^log-writes"; then
	log_unsupported "the dm-log-writes target is not registered"
fi
# Setting up the log-writes device over loop devices panicked the 4.18
# kernel of AlmaLinux 8 in the block layer (blk_mq_run_hw_queues from a
# kblockd requeue, and once a corrupted slab freelist) in two of two CI
# runs; kernels from 5.14 ran the group without trouble.
if [[ $(linux_version) -lt $(linux_version "5.14") ]]; then
	log_unsupported "dm-log-writes crash states need Linux 5.14 or newer"
fi

# Start from a clean slate (e.g. mountpoint directories an interrupted
# earlier run left behind).
tmpfile_dmlog_cleanup

log_pass
