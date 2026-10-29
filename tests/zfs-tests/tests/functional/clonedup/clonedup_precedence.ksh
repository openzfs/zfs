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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A stronger run replaces a weaker running one, in the order
#	quick < default < full.  An equal or weaker request is
#	refused, and so is a dry run of any rank, which leaves the
#	running one alone.  A real run of any rank replaces a dry
#	run.
#

verify_runnable "global"

# Whether zpool status -j shows "key":value.
function cd_pr_status # key value
{
	zpool status -j $TESTPOOL | grep -q "\"$1\":$2"
}

log_assert "run precedence is quick < default < full"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_sync

clonedup_hold
log_must zpool clonedup -q $TESTPOOL
clonedup_wait_running
clonedup_jstat_is clonedup_flags.quick true
log_mustnot zpool clonedup -q $TESTPOOL
log_mustnot zpool clonedup -f -n $TESTPOOL
log_mustnot cd_pr_status dry_run true
clonedup_jstat_is clonedup_flags.quick true

log_must zpool clonedup $TESTPOOL
log_must clonedup_is_running
clonedup_jstat_is clonedup_flags.quick false
log_mustnot zpool clonedup -q $TESTPOOL
log_mustnot zpool clonedup $TESTPOOL

log_must zpool clonedup -f $TESTPOOL
log_must clonedup_is_running
clonedup_jstat_is clonedup_flags.full true
log_mustnot zpool clonedup -f $TESTPOOL
log_mustnot zpool clonedup $TESTPOOL
log_mustnot zpool clonedup -q $TESTPOOL
log_mustnot zpool clonedup -f -n $TESTPOOL
log_mustnot cd_pr_status dry_run true
clonedup_jstat_is clonedup_flags.full true
clonedup_jstat_is clonedup_flags.dry_run false

clonedup_release
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED

# A real quick run replaces a dry full run.
clonedup_hold
log_must zpool clonedup -f -n $TESTPOOL
clonedup_wait_running
log_must cd_pr_status dry_run true
log_mustnot zpool clonedup -q -n $TESTPOOL
log_must zpool clonedup -q $TESTPOOL
log_must clonedup_is_running
log_mustnot cd_pr_status dry_run true
log_must cd_pr_status quick true
clonedup_release
log_must zpool wait -t clonedup $TESTPOOL

log_pass "run precedence is quick < default < full"
