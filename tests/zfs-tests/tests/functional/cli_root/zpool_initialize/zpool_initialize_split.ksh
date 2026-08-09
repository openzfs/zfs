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
# Initializing state is preserved across zpool split.
#
# STRATEGY:
# 1. Create a pool with a two-way mirror.
# 2. Start initializing both devices.
# 3. Split the pool. Ensure initializing continues on the original.
# 4. Import the new pool. Ensure initializing resumes on it.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"
POOL2="${TESTPOOL}_split"

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2

log_must zpool initialize $TESTPOOL $DISK1 $DISK2
orig_prog1="$(initialize_progress $TESTPOOL $DISK1)"
orig_prog2="$(initialize_progress $TESTPOOL $DISK2)"
[[ -z "$orig_prog1" ]] && log_fail "Initializing did not start"

log_must zpool split $TESTPOOL $TESTPOOL1 $DISK2

# Ensure initializing continued as expected on the original pool.
[[ "$(initialize_progress $TESTPOOL $DISK1)" -ge "$orig_prog1" ]] || \
        log_fail "Initializing lost progress on original pool"
log_mustnot eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

log_must zpool import $TESTPOOL1

[[ "$(initialize_progress $TESTPOOL1 $DISK2)" -ge "$orig_prog2" ]] || \
        log_fail "Initializing lost progress on split pool"
log_mustnot eval "initialize_prog_line $TESTPOOL1 $DISK1 | grep suspended"

log_pass "Initializing behaves as expected on zpool split"
