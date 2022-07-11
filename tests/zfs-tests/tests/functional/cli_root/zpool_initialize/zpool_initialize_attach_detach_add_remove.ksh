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
# Detaching/attaching, adding/removing data devices works with initializing.
#
# STRATEGY:
# 1. Create a single-disk pool.
# 2. Start initializing.
# 3. Attach a second disk, ensure initializing continues.
# 4. Detach the second disk, ensure initializing continues.
# 5. Add a second disk, ensure initializing continues.
# 6. Remove the first disk, ensure initializing stops.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"

log_must zpool create -f $TESTPOOL $DISK1

log_must zpool initialize $TESTPOOL $DISK1
progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Initializing did not start"

log_must zpool attach $TESTPOOL $DISK1 $DISK2
new_progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ "$progress" -le "$new_progress" ]] || \
        log_fail "Lost initializing progress on demotion to child vdev"
progress="$new_progress"

log_must zpool detach $TESTPOOL $DISK2
new_progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ "$progress" -le "$new_progress" ]] || \
        log_fail "Lost initializing progress on promotion to top vdev"
progress="$new_progress"

log_must zpool add $TESTPOOL $DISK2
log_must zpool remove $TESTPOOL $DISK1
[[ -z "$(initialize_prog_line $TESTPOOL $DISK1)" ]] || \
        log_fail "Initializing continued after initiating removal"

log_pass "Initializing worked as expected across attach/detach and add/remove"
