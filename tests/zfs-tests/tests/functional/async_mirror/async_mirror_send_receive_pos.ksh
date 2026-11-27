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
#	A snapshot can be sent from a pool with an async mirror backlog and
#	received into another pool with an async mirror enabled.
#
# STRATEGY:
#	1. Create source and destination async mirrors.
#	2. Suspend copying, write and snapshot source data, then send it to the
#	   destination pool.
#	3. Compare the source and received file digests.
#	4. Resume copying and verify both async mirrors become idle.
#

verify_runnable "global"

typeset source_pool=${TESTPOOL}_source
typeset source_leader=$ASYNC_MIRROR_DIR/source-leader
typeset source_lagging=$ASYNC_MIRROR_DIR/source-lagging
typeset source_data=/$source_pool/fs/data
typeset main_leader=$ASYNC_MIRROR_LEADER
typeset main_lagging=$ASYNC_MIRROR_LAGGING
typeset main_pool=$TESTPOOL

function cleanup_send_receive
{
	async_mirror_suspend_copy 0
	async_mirror_suspend_release 0
	if poolexists $source_pool; then
		destroy_pool $source_pool
	fi
	ASYNC_MIRROR_LEADER=$main_leader
	ASYNC_MIRROR_LAGGING=$main_lagging
	TESTPOOL=$main_pool
	ASYNC_MIRROR_VDEVS="$main_leader $main_lagging \
	    $ASYNC_MIRROR_EXTRA $source_leader $source_lagging"
	cleanup_async_mirror
}

ASYNC_MIRROR_VDEVS="$ASYNC_MIRROR_VDEVS $source_leader $source_lagging"
log_onexit cleanup_send_receive
log_assert "Send and receive preserve data across async mirrors."

async_mirror_create_pool
async_mirror_enable

log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $source_leader $source_lagging
log_must zpool create -f $source_pool mirror $source_leader $source_lagging
ASYNC_MIRROR_LEADER=$source_leader
ASYNC_MIRROR_LAGGING=$source_lagging
TESTPOOL=$source_pool
async_mirror_enable
async_mirror_suspend_copy 1
log_must zfs create $source_pool/fs
log_must file_write -o create -f $source_data -b 131072 -c 16 -d R
log_must zfs snapshot $source_pool/fs@snap
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0

ASYNC_MIRROR_LEADER=$main_leader
ASYNC_MIRROR_LAGGING=$main_lagging
TESTPOOL=$main_pool
async_mirror_suspend_copy 1
log_must eval "zfs send $source_pool/fs@snap | zfs receive $TESTPOOL/received"
typeset source_digest=$(xxh128digest $source_data)
typeset received_digest=$(xxh128digest /$TESTPOOL/received/data)
log_must test "$source_digest" == "$received_digest"
log_must test "$(get_async_mirror_stat active)" -eq 1

async_mirror_suspend_copy 0
log_must wait_async_mirror_idle 600
TESTPOOL=$source_pool
log_must wait_async_mirror_idle 600
TESTPOOL=$main_pool

log_pass "Send and receive preserve data with async mirrors enabled."
