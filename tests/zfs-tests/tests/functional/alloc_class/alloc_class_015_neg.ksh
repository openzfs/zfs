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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Setting the alloc_bias vdev property to invalid values or on
#	unsupported vdev types fails.
#
# STRATEGY:
#	1. Create a pool with a normal mirror and a log vdev.
#	2. Verify setting alloc_bias on a leaf vdev fails.
#	3. Verify setting alloc_bias=log fails.
#	4. Verify setting alloc_bias to an unknown value fails.
#	5. Verify setting alloc_bias on a log vdev fails.
#	6. Verify setting alloc_bias=special fails when allocation_classes
#	   feature is not enabled.
#	7. Verify converting the last normal vdev fails.
#

verify_runnable "global"

claim="Setting alloc_bias to invalid values or on unsupported vdevs fails"

log_assert $claim
log_onexit cleanup

log_must disk_setup

# Create a pool with a normal mirror and a log vdev.
log_must zpool create $TESTPOOL \
    mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    log $CLASS_DISK0

NORMAL_VDEV=$(zpool list -v -H $TESTPOOL | awk '$1 ~ /^mirror/ {print $1; exit}')
log_note "Normal vdev: $NORMAL_VDEV"

# Setting alloc_bias on a leaf vdev must fail.
log_mustnot zpool set alloc_bias=special $TESTPOOL $ZPOOL_DISK0

# Setting alloc_bias=log must fail (log vdevs must be removed and re-added).
log_mustnot zpool set alloc_bias=log $TESTPOOL $NORMAL_VDEV

# Setting alloc_bias to an unknown value must fail.
log_mustnot zpool set alloc_bias=bogus $TESTPOOL $NORMAL_VDEV

# Setting alloc_bias on a log vdev must fail.
# CLASS_DISK0 is a single-disk (non-mirror) top-level log vdev.
log_mustnot zpool set alloc_bias=special $TESTPOOL $CLASS_DISK0

log_must zpool destroy -f $TESTPOOL

# Verify setting alloc_bias=special fails when allocation_classes is disabled.
# Create a pool with the allocation_classes feature explicitly disabled.
log_must zpool create -o feature@allocation_classes=disabled $TESTPOOL \
    mirror $ZPOOL_DISK0 $ZPOOL_DISK1

NORMAL_VDEV=$(zpool list -v -H $TESTPOOL | awk '$1 ~ /^mirror/ {print $1; exit}')
log_mustnot zpool set alloc_bias=special $TESTPOOL $NORMAL_VDEV
log_mustnot zpool set alloc_bias=dedup $TESTPOOL $NORMAL_VDEV

log_must zpool destroy -f $TESTPOOL

# Verify that converting the last normal-class top-level vdev fails.
# A pool must always retain at least one normal vdev.
log_must zpool create $TESTPOOL \
    mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    special mirror $CLASS_DISK0 $CLASS_DISK1

NORMAL_VDEV=$(zpool list -v -H $TESTPOOL | awk '$1 ~ /^mirror/ {print $1; exit}')
log_mustnot zpool set alloc_bias=special $TESTPOOL $NORMAL_VDEV
log_mustnot zpool set alloc_bias=dedup $TESTPOOL $NORMAL_VDEV

log_must zpool destroy -f $TESTPOOL
log_pass $claim
