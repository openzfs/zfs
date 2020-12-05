#!/bin/ksh -p
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
# Trimming automatically resumes across offline/online.
#
# STRATEGY:
# 1. Create a pool with a two-way mirror, prepare blocks to trim.
# 2. Start trimming one of the disks and verify that trimming is active.
# 3. Offline the disk.
# 4. Online the disk.
# 5. Verify that trimming resumes and progress does not regress.
# 6. Suspend trimming.
# 7. Repeat steps 3-4 and verify that trimming does not resume.
#

DISK1=${DISKS%% *}
DISK2="$(echo $DISKS | cut -d' ' -f2)"

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2 -O recordsize=4k
sync_and_rewrite_some_data_a_few_times $TESTPOOL

log_must zpool trim -r 1 $TESTPOOL $DISK1

log_must zpool offline $TESTPOOL $DISK1

progress="$(trim_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Trimming did not start"

log_must zpool online $TESTPOOL $DISK1

new_progress="$(trim_progress $TESTPOOL $DISK1)"
[[ -z "$new_progress" ]] && \
    log_fail "Trimming did not restart after onlining"
[[ "$progress" -le "$new_progress" ]] || \
    log_fail "Trimming lost progress after onlining"
log_mustnot eval "trim_prog_line $TESTPOOL $DISK1 | grep suspended"

log_must zpool trim -s $TESTPOOL $DISK1
action_date="$(trim_prog_line $TESTPOOL $DISK1 | \
    sed 's/.*ed at \(.*\)).*/\1/g')"
log_must zpool offline $TESTPOOL $DISK1
log_must zpool online $TESTPOOL $DISK1
new_action_date=$(trim_prog_line $TESTPOOL $DISK1 | \
    sed 's/.*ed at \(.*\)).*/\1/g')
[[ "$action_date" != "$new_action_date" ]] && \
    log_fail "Trimming action date did not persist across offline/online"
log_must eval "trim_prog_line $TESTPOOL $DISK1 | grep suspended"

log_pass "Trimming performs as expected across offline/online"
