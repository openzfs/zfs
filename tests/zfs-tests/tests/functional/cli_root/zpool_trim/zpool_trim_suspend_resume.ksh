#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
# Suspending and resuming trimming works.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Start trimming and verify that trimming is active.
# 3. Wait 3 seconds, then suspend trimming and verify that the progress
#    reporting says so.
# 4. Wait 3 seconds and ensure trimming progress doesn't advance.
# 5. Restart trimming and verify that the progress doesn't regress.
#

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	if [[ -d "$TESTDIR" ]]; then
		rm -rf "$TESTDIR"
	fi
}

LARGEFILE="$TESTDIR/largefile"

log_must mkdir "$TESTDIR"
log_must truncate -s 10G "$LARGEFILE"
log_must zpool create -f $TESTPOOL $LARGEFILE

log_must zpool trim -r 256M $TESTPOOL
sleep 2

[[ -z "$(trim_progress $TESTPOOL $LARGEFILE)" ]] && \
    log_fail "Trimming did not start"

sleep 3
log_must zpool trim -s $TESTPOOL
log_must eval "trim_prog_line $TESTPOOL $LARGEFILE | grep suspended"
progress="$(trim_progress $TESTPOOL $LARGEFILE)"

sleep 3
[[ "$progress" -eq "$(trim_progress $TESTPOOL $LARGEFILE)" ]] || \
	log_fail "Trimming progress advanced while suspended"

log_must zpool trim $TESTPOOL $LARGEFILE
[[ "$progress" -le "$(trim_progress $TESTPOOL $LARGEFILE)" ]] ||
	log_fail "Trimming progress regressed after resuming"

log_pass "Suspend + resume trimming works as expected"
