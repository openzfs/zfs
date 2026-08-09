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
# Cancelling and suspending initialize doesn't work if not all specified vdevs
# are being initialized.
#
# STRATEGY:
# 1. Create a three-disk pool.
# 2. Start initializing and verify that initializing is active.
# 3. Try to cancel and suspend initializing on the non-initializing disks.
# 4. Try to re-initialize the currently initializing disk.
#

DISK1=${DISKS%% *}
DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

log_must zpool list -v
log_must zpool create -f $TESTPOOL $DISK1 $DISK2 $DISK3
log_must zpool initialize $TESTPOOL $DISK1

[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] && \
    log_fail "Initialize did not start"

log_mustnot zpool initialize -c $TESTPOOL $DISK2
log_mustnot zpool initialize -c $TESTPOOL $DISK2 $DISK3

log_mustnot zpool initialize -s $TESTPOOL $DISK2
log_mustnot zpool initialize -s $TESTPOOL $DISK2 $DISK3

log_mustnot zpool initialize $TESTPOOL $DISK1

log_pass "Nonsensical initialize operations fail"
