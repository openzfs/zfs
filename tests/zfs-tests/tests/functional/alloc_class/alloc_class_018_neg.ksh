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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026, TrueNAS
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	A spare with a mismatched alloc_bias cannot replace a vdev of a
#	different allocation class.
#
# STRATEGY:
#	1. Create a pool with a normal mirror, a single special vdev, and
#	   two spares: one unbiased, one set to alloc_bias=special.
#	2. Verify an unbiased spare cannot replace the special vdev member.
#	3. Verify a special-biased spare cannot replace a normal vdev member.
#	4. Verify setting alloc_bias=log on a non-spare normal vdev fails.
#	5. Verify setting alloc_bias=special on a spare fails when the
#	   allocation_classes feature is disabled.
#

verify_runnable "global"

claim="spare with mismatched alloc_bias is rejected as a replacement"

log_assert $claim
log_onexit cleanup

log_must disk_setup

# CLASS_DISK2: unbiased spare; CLASS_DISK3: special-biased spare.
log_must zpool create -f $TESTPOOL \
    mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    special $CLASS_DISK0 \
    spare $CLASS_DISK2 $CLASS_DISK3
log_must zpool set alloc_bias=special $TESTPOOL $CLASS_DISK3

log_must zpool offline -f $TESTPOOL $CLASS_DISK0

# Unbiased spare cannot replace a special vdev member.
log_mustnot zpool replace $TESTPOOL $CLASS_DISK0 $CLASS_DISK2

log_must zpool online $TESTPOOL $CLASS_DISK0
log_must zpool clear $TESTPOOL

log_must zpool offline -f $TESTPOOL $ZPOOL_DISK0

# Special-biased spare cannot replace a normal vdev member.
log_mustnot zpool replace $TESTPOOL $ZPOOL_DISK0 $CLASS_DISK3

log_must zpool online $TESTPOOL $ZPOOL_DISK0
log_must zpool clear $TESTPOOL

log_must zpool destroy -f $TESTPOOL

# Setting alloc_bias=log on a non-spare top-level vdev must fail.
log_must zpool create $TESTPOOL mirror $ZPOOL_DISK0 $ZPOOL_DISK1
NORMAL_VDEV=$(zpool list -v -H $TESTPOOL | awk '$1 ~ /^mirror/ {print $1; exit}')
log_mustnot zpool set alloc_bias=log $TESTPOOL $NORMAL_VDEV
log_must zpool destroy -f $TESTPOOL

# Setting alloc_bias=special on a spare fails when feature is disabled.
log_must zpool create -o feature@allocation_classes=disabled $TESTPOOL \
    mirror $ZPOOL_DISK0 $ZPOOL_DISK1 spare $CLASS_DISK2
log_mustnot zpool set alloc_bias=special $TESTPOOL $CLASS_DISK2
log_mustnot zpool set alloc_bias=dedup $TESTPOOL $CLASS_DISK2
log_must zpool destroy -f $TESTPOOL

log_pass $claim
