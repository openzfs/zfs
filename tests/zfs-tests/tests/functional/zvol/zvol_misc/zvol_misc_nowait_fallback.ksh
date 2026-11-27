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

. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
#	A blk-mq zvol hands a request that would have to wait in its submitter
#	to the zvol_nowait taskq, which waits instead and processes it.  The
#	first write after the zvol is opened is such a request: it opens the
#	ZIL, which takes zv_suspend_lock as writer.
#
# STRATEGY:
# 1. Create a blk-mq zvol with the default settings, with which every
#    request that is queued takes this path, whether or not the kernel
#    marks it non-blocking.
# 2. Open it and write a block with O_DIRECT and RWF_NOWAIT: the zvol_nowait
#    taskq receives one request during the write.
# 3. Write a second block: the ZIL is open, so the taskq receives none.
# 4. Read both blocks back with RWF_NOWAIT: the data is there, and the taskq
#    receives no request.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Only Linux has blk-mq zvols"
fi

typeset vol=$TESTPOOL/nowait_fallback

function cleanup
{
	datasetexists "$vol" && destroy_dataset "$vol"
	log_must restore_tunable VOL_USE_BLK_MQ
	log_must restore_tunable VOL_REQUEST_SYNC
}

log_assert "A blk-mq zvol's first write goes through the zvol_nowait taskq"
log_onexit cleanup

log_must save_tunable VOL_USE_BLK_MQ
log_must save_tunable VOL_REQUEST_SYNC
set_blk_mq 1
set_zvol_sync 0

log_must zfs create -V 64M "$vol"

typeset expected="w33554432 ok 1
w37748736 ok 0
r33554432 ok 0
r37748736 ok 0"
typeset out

out=$(nowait_io "$vol" w33554432 w37748736 r33554432 r37748736)

log_note "$out"
if [[ "$out" != "$expected" ]]; then
	log_fail "Expected: $expected"
fi

log_pass "A blk-mq zvol's first write goes through the zvol_nowait taskq"
