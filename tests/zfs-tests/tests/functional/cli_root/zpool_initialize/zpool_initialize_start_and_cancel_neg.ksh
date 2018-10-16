#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
