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
# Copyright (c) 2016 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Attaching data devices works with initializing for AnyRAID1.
#
# STRATEGY:
# 1. Create an AnyRAID1 pool.
# 2. Start initializing of the first disk.
# 3. Attach a third disk, ensure initializing continues.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

log_must zpool create -f $TESTPOOL anymirror1 $DISK1 $DISK2

log_must zpool initialize $TESTPOOL $DISK1
progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Initializing did not start"

log_must zpool attach $TESTPOOL anymirror1-0 $DISK3
new_progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ "$progress" -le "$new_progress" ]] || \
        log_fail "Lost initializing progress on AnyRAID1 attach"
progress="$new_progress"

log_pass "Attaching data devices works with initializing for AnyRAID1"
