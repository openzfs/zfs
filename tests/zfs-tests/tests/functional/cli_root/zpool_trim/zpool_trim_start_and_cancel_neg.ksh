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
# Cancelling and suspending trim doesn't work if not all specified vdevs
# are being trimmed.
#
# STRATEGY:
# 1. Create a three-disk pool.
# 2. Start trimming and verify that trimming is active.
# 3. Try to cancel and suspend trimming on the non-trimming disks.
# 4. Try to re-trim the currently trimming disk.
#

DISK1=${DISKS%% *}
DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

log_must zpool list -v
log_must zpool create -f $TESTPOOL $DISK1 $DISK2 $DISK3 -O recordsize=4k
sync_and_rewrite_some_data_a_few_times $TESTPOOL

log_must zpool trim -r 1 $TESTPOOL $DISK1

[[ -z "$(trim_progress $TESTPOOL $DISK1)" ]] && \
    log_fail "Trim did not start"

log_mustnot zpool trim -c $TESTPOOL $DISK2
log_mustnot zpool trim -c $TESTPOOL $DISK2 $DISK3

log_mustnot zpool trim -s $TESTPOOL $DISK2
log_mustnot zpool trim -s $TESTPOOL $DISK2 $DISK3

log_mustnot zpool trim $TESTPOOL $DISK1

log_pass "Nonsensical trim operations fail"
