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
#	'zpool status' reports the lagging child and its copy progress.
#
# STRATEGY:
#	1. Create a two-way mirror and turn on the async mirror.
#	2. Stop the copy and write data to make the lagging child fall behind.
#	3. Check the child annotation and the txg and byte counts.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_suspend_copy 1
async_mirror_enable
async_mirror_write_files a 2

typeset status=$(zpool status -P $TESTPOOL)
log_note "$status"
echo "$status" | grep -q ' (lagging)' ||
	log_fail "The lagging child has no status annotation"
# The line names the state of the child: the copy is stopped, so "behind".
echo "$status" | grep -q \
	'async mirror: lagging child (behind) at txg .* of .*; .* pending' ||
	log_fail "The status has no async mirror progress line"

log_pass "zpool status reports the async mirror progress."
