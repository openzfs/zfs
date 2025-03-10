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
# Detaching/attaching, adding/removing data devices works with trimming.
#
# STRATEGY:
# 1. Create a single-disk pool.
# 2. Start trimming.
# 3. Attach a second disk, ensure trimming continues.
# 4. Detach the second disk, ensure trimming continues.
# 5. Add a second disk, ensure trimming continues.
# 6. Remove the first disk, ensure trimming stops.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"

log_must zpool create -f $TESTPOOL $DISK1

log_must zpool trim -r 128M $TESTPOOL $DISK1
progress="$(trim_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Trim did not start"

log_must zpool attach $TESTPOOL $DISK1 $DISK2
new_progress="$(trim_progress $TESTPOOL $DISK1)"
[[ "$progress" -le "$new_progress" ]] || \
        log_fail "Lost trimming progress on demotion to child vdev"
progress="$new_progress"

log_must zpool detach $TESTPOOL $DISK2
new_progress="$(trim_progress $TESTPOOL $DISK1)"
[[ "$progress" -le "$new_progress" ]] || \
        log_fail "Lost trimming progress on promotion to top vdev"
progress="$new_progress"

log_must zpool add $TESTPOOL $DISK2
log_must zpool remove $TESTPOOL $DISK1
[[ -z "$(trim_prog_line $TESTPOOL $DISK1)" ]] || \
        log_fail "Trimming continued after initiating removal"

log_pass "Trimming worked as expected across attach/detach and add/remove"
