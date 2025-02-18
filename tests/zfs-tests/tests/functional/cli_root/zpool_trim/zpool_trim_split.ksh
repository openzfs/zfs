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
# Trimming state is preserved across zpool split.
#
# STRATEGY:
# 1. Create a pool with a two-way mirror.
# 2. Start trimming both devices.
# 3. Split the pool. Ensure trimming continues on the original.
# 4. Import the new pool. Ensure trimming resumes on it.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"
POOL2="${TESTPOOL}_split"

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2

log_must zpool trim $TESTPOOL $DISK1 $DISK2
orig_prog1="$(trim_progress $TESTPOOL $DISK1)"
orig_prog2="$(trim_progress $TESTPOOL $DISK2)"
[[ -z "$orig_prog1" ]] && log_fail "Trimming did not start"

log_must zpool split $TESTPOOL $TESTPOOL1 $DISK2

# Ensure trimming continued as expected on the original pool.
[[ "$(trim_progress $TESTPOOL $DISK1)" -ge "$orig_prog1" ]] || \
        log_fail "Trimming lost progress on original pool"
log_mustnot eval "trim_prog_line $TESTPOOL $DISK1 | grep suspended"

log_must zpool import $TESTPOOL1

[[ "$(trim_progress $TESTPOOL1 $DISK2)" -ge "$orig_prog2" ]] || \
        log_fail "Trimming lost progress on split pool"
log_mustnot eval "trim_prog_line $TESTPOOL1 $DISK1 | grep suspended"

log_pass "Trimming behaves as expected on zpool split"
