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
#	A read-write import with the lagging child missing starts a new
#	lineage once, and records the fork.  While the fork is open,
#	zpool checkpoint is refused.  With both children back, a resilver,
#	and the async mirror off, the lineage is cleared and a checkpoint works.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files, wait until the lagging child has caught up, and
#	   verify that the pool has a lineage and no fork.
#	3. Export the pool.  Hide the lagging child, and import the leader
#	   alone.  Verify the fork: a new lineage, its parent, and the
#	   missing child.  Verify that the labels of the leader hold the new
#	   lineage, and that those of the lagging child still hold the first
#	   lineage.
#	4. Export and import the leader alone again.  Verify that the
#	   lineage did not change.
#	5. Verify that zpool checkpoint is refused.
#	6. Export the pool, bring the lagging child back, import the pool,
#	   and wait for the resilver.
#	7. Turn the async mirror off.  Verify that the lineage is gone, and that a
#	   checkpoint can be taken and discarded.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset first=$(get_async_mirror_lineage lineage)
log_must test "$first" != 0
log_must test "$(get_async_mirror_lineage lineage_parent)" == 0

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zpool export $TESTPOOL
async_mirror_import_leader
typeset second=$(get_async_mirror_lineage lineage)
log_must test "$second" != 0
log_must test "$second" != "$first"
log_must test "$(get_async_mirror_lineage lineage_parent)" == "$first"
log_must test "$(get_async_mirror_lineage lineage_absent)" == "$guid"
log_must test "$(get_label_lineage $ASYNC_MIRROR_LEADER)" == "$second"
log_must test "$(get_label_lineage $ASYNC_MIRROR_LAGGING_HIDDEN)" == "$first"

log_must zpool export $TESTPOOL
log_must zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(get_async_mirror_lineage lineage)" == "$second"

log_mustnot zpool checkpoint $TESTPOOL

log_must zpool export $TESTPOOL
async_mirror_unhide_lagging
async_mirror_import
log_must wait_async_mirror_resilvered

async_mirror_disable
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_lineage lineage)" == 0
log_must zpool checkpoint $TESTPOOL
log_must zpool checkpoint -d -w $TESTPOOL

log_pass "A fork is recorded once and blocks a checkpoint until resolved."
