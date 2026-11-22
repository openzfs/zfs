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
#	A metadata block with one damaged copy on the leader and one good
#	copy does not stop the copy, and the lagging child gets the good
#	bytes in both copies.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. With the copy stopped, write file b and damage the first copy of
#	   its indirect block on the leader.  Export, restart the copy, and
#	   import.
#	3. Verify that the copy finished with no failure and no data error.
#	4. Import the lagging child alone, scrub it, and verify that the
#	   scrub finds no checksum error and that b is intact.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_damage_backlog /$TESTPOOL/b 1 1
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat data_errors)" == 0
typeset digest=$(xxh128digest /$TESTPOOL/b)
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must zpool scrub -w $TESTPOOL
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/b)"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A damaged copy of a metadata block does not reach the child."
