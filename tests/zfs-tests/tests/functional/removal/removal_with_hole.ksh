#! /bin/ksh -p
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
# Copyright (c) 2025 by Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

log_onexit default_cleanup_noexit
DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

log_must zpool create $TESTPOOL $DISK1 log $DISK2
log_must zpool add $TESTPOOL $DISK3
log_must zpool remove $TESTPOOL $DISK2
log_must zpool remove $TESTPOOL $DISK1

log_pass "Removal with a hole as the first other device doesn't panic."
