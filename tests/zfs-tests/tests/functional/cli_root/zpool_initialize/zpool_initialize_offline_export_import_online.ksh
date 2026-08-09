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
# Miscellaneous complex sequences of operations function as expected.
#
# STRATEGY:
# 1. Create a pool with a two-way mirror.
# 2. Start initializing, offline, export, import, online and verify that
#    initializing state is preserved / initializing behaves as expected
#    at each step.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2

log_must zpool initialize $TESTPOOL $DISK1
log_must zpool offline $TESTPOOL $DISK1
progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Initializing did not start"
log_mustnot eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

new_progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ -z "$new_progress" ]] && log_fail "Initializing did not start after import"
[[ "$new_progress" -ge "$progress" ]] || \
    log_fail "Initializing lost progress after import"
log_mustnot eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

log_must zpool online $TESTPOOL $DISK1
new_progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ "$new_progress" -ge "$progress" ]] || \
    log_fail "Initializing lost progress after online"

log_pass "Initializing behaves as expected at each step of:" \
    "initialize + offline + export + import + online"
