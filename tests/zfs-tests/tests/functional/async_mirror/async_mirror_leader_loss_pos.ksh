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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A leader read failure during backlog replay keeps a complete older
#	copy on the lagging child.
#
# STRATEGY:
#	1. Write and copy a baseline file to both children.
#	2. Suspend copying, write a newer file, then fail leader reads.
#	3. Resume copying and wait for the replay to freeze on the read error.
#	4. Import the lagging child alone and verify the older baseline copy.
#

verify_runnable "global"

log_onexit cleanup_async_mirror
log_assert "A leader failure leaves the lagging child at a complete txg."

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files baseline 1
log_must wait_async_mirror_idle
typeset digest=$(async_mirror_digests baseline 1)

async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/newer 16
sync_pool $TESTPOOL
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LEADER)
log_must zinject -d $guid -e io -T read $TESTPOOL
async_mirror_suspend_copy 0
log_must wait_async_mirror_frozen 600
log_must zinject -c all
log_must test "$(get_async_mirror_stat frozen)" -eq 1
typeset status_output=$(zpool status $TESTPOOL 2>&1)
echo "$status_output" | grep -Fq "lagging child (frozen)" ||
    log_fail "zpool status did not show the frozen mirror state"
status_output=$(zpool status -x $TESTPOOL 2>&1)
if ! echo "$status_output" |
    grep -Fq "asynchronous mirror copy is frozen"; then
	log_note "zpool status -x: $status_output"
	log_fail "zpool status -x did not report the frozen mirror"
fi
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$digest" == "$(async_mirror_digests baseline 1)"
log_must test ! -e /$TESTPOOL/newer
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "The lagging child remains a complete older copy after leader loss."
