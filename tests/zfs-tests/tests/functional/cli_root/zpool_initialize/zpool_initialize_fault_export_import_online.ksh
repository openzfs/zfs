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
# Copyright (c) 2021 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Miscellaneous complex sequences of operations function as expected.
#
# STRATEGY:
# 1. Create a pool with a two-way mirror.
# 2. Start initializing, fault, export, import, online and verify along
#    the way that the initializing was cancelled and not restarted.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2

log_must zpool initialize $TESTPOOL $DISK1
progress="$(initialize_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Initializing did not start"

log_must zpool offline -f $TESTPOOL $DISK1
log_must check_vdev_state $TESTPOOL $DISK1 "FAULTED"
log_must eval "zpool status -i $TESTPOOL | grep $DISK1 | grep uninitialized"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_must check_vdev_state $TESTPOOL $DISK1 "FAULTED"
log_must eval "zpool status -i $TESTPOOL | grep $DISK1 | grep uninitialized"

log_must zpool online $TESTPOOL $DISK1
log_must zpool clear $TESTPOOL $DISK1
log_must check_vdev_state $TESTPOOL $DISK1 "ONLINE"
log_must eval "zpool status -i $TESTPOOL | grep $DISK1 | grep uninitialized"

log_pass "Initializing behaves as expected at each step of:" \
    "initialize + fault + export + import + online"
