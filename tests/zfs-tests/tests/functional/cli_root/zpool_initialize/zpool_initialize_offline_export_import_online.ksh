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
