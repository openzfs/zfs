#!/bin/ksh -p
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
