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
#	The vdev config marks the lagging child while the async mirror is on.  The
#	mark is in the labels of the leader.  It is also in the labels of the
#	lagging child, which the switch on rewrites at the txg that the child
#	holds, so that an import of the child alone finds the mark.
#	Turning the async mirror off clears the mark.  A
#	failed replay clears it too, because the DTLs then say what the child
#	is missing.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write data, and verify the mark in the labels of both children,
#	   and that the config txg of the lagging child is not above its
#	   newest uberblock.
#	3. Stop the copy, change the config of the pool, and sync.  Verify
#	   that the leader has new labels, and that no label write of the
#	   pool reached the lagging child: its config txg did not change.
#	4. Turn the async mirror off, and verify that the mark is gone.
#	5. Turn the async mirror on again, and verify the mark.
#	6. Inject write errors on the lagging child until the replay fails.
#	7. Wait for the release of the records, and verify that the mark
#	   is gone.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

function has_mark # vdev file
{
	zdb -l $1 | grep -q async_mirror_lagging
}

async_mirror_create_pool
log_mustnot has_mark $ASYNC_MIRROR_LEADER
async_mirror_enable

async_mirror_write_files file 2
log_must wait_async_mirror_idle
log_must has_mark $ASYNC_MIRROR_LEADER
log_must has_mark $ASYNC_MIRROR_LAGGING
log_must label_txg_held $ASYNC_MIRROR_LAGGING

#
# The lagging child is at the newest txg here, so a label that the pool
# wrote to it by mistake would pass label_txg_held.  With the copy stopped,
# the child stays behind while the pool writes new labels.
#
typeset -i ltxg=$(get_label_txg $ASYNC_MIRROR_LAGGING)
typeset -i leader=$(get_label_txg $ASYNC_MIRROR_LEADER)
async_mirror_suspend_copy 1
log_must zpool set comment="async mirror" $TESTPOOL
sync_pool $TESTPOOL
log_must test $(get_label_txg $ASYNC_MIRROR_LEADER) -gt $leader
log_must test $(get_label_txg $ASYNC_MIRROR_LAGGING) -eq $ltxg
log_must label_txg_held $ASYNC_MIRROR_LAGGING
log_must has_mark $ASYNC_MIRROR_LAGGING
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle

async_mirror_disable
sync_pool $TESTPOOL
log_mustnot has_mark $ASYNC_MIRROR_LEADER
log_mustnot has_mark $ASYNC_MIRROR_LAGGING

async_mirror_enable
async_mirror_write_files more 2
log_must wait_async_mirror_idle
log_must has_mark $ASYNC_MIRROR_LEADER

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files fail 2
log_must wait_async_mirror_failed
log_must zinject -c all
log_must wait_async_mirror_feature enabled
sync_pool $TESTPOOL
log_mustnot has_mark $ASYNC_MIRROR_LEADER
log_must wait_async_mirror_resilvered
sync_pool $TESTPOOL
log_mustnot has_mark $ASYNC_MIRROR_LAGGING

log_pass "The vdev config marks the lagging child while the async mirror is on."
