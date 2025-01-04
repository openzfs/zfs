#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Suspending and resuming initializing works.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Start initializing and verify that initializing is active.
# 3. Wait 3 seconds, then suspend initializing and verify that the progress
#    reporting says so.
# 4. Wait 5 seconds and ensure initializing progress doesn't advance.
# 5. Restart initializing and verify that the progress doesn't regress.
#

DISK1=${DISKS%% *}

log_must zpool create -f $TESTPOOL $DISK1
log_must zpool initialize $TESTPOOL

[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] && \
    log_fail "Initializing did not start"

sleep 5
log_must zpool initialize -s $TESTPOOL
log_must eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"
progress="$(initialize_progress $TESTPOOL $DISK1)"

sleep 3
[[ "$progress" -eq "$(initialize_progress $TESTPOOL $DISK1)" ]] || \
        log_fail "Initializing progress advanced while suspended"

log_must zpool initialize $TESTPOOL $DISK1
[[ "$progress" -le "$(initialize_progress $TESTPOOL $DISK1)" ]] ||
        log_fail "Initializing progress regressed after resuming"

log_pass "Suspend + resume initializing works as expected"
