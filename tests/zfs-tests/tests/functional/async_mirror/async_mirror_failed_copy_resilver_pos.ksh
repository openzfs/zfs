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
#	After a failed replay, the lagging child is resilvered from the
#	leader, and the records are released.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write data and wait for the replay to catch up.
#	3. Inject write errors on the lagging child, and write more data.
#	4. Wait until the replay fails.
#	5. Clear the injection and wait for the resilver to finish.
#	6. Verify that the feature returns to enabled, that the data is
#	   correct, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files file 2
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files more 2
typeset digests="$(async_mirror_digests file 2)
$(async_mirror_digests more 2)"
log_must wait_async_mirror_failed
log_must zinject -c all

log_must wait_async_mirror_feature enabled
log_must wait_async_mirror_resilvered
log_must test "$digests" == "$(async_mirror_digests file 2)
$(async_mirror_digests more 2)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "A failed replay is repaired by a resilver."
