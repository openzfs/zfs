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
#	A data block that is damaged on the leader before the copy, with no
#	good copy, is reported and skipped, and the copy goes on.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. With the copy stopped, write file b and damage its first data
#	   block on the leader.  Export, restart the copy, and import.
#	3. Verify that the copy finished with no failure and no freeze,
#	   that the kstat counts a data error and the tries before it, and
#	   that zpool status names b.
#	4. Import the lagging child alone.  Verify files a, and that b cannot
#	   be read: the lagging child kept older bytes for the damaged block.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"

async_mirror_damage_backlog /$TESTPOOL/b 0 1
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat data_errors)" -ge 1
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must test "$(get_async_mirror_stat read_retries)" -gt 0
log_must eval "zpool status -v $TESTPOOL | grep -q '/$TESTPOOL/b\$'"
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_mustnot eval "cat /$TESTPOOL/b > /dev/null"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A damaged data block is reported and skipped."
