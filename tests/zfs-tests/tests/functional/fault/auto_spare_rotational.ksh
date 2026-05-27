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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# ZED prefers the smallest sufficient spare when replacing a faulted
# special vdev, regardless of spare list order.
#
# The 'rotational' property is persisted in the pool config for all leaf
# vdevs so that spare selection can match device type even after the
# original disk is gone.  ZED sorts spares preferring matching rotational
# and, among equally-matching spares, the smallest sufficient one.
#
# STRATEGY:
# 1. Create a pool with a normal mirror, a special mirror, and two file
#    spares of different sizes.  List the larger spare first so that the
#    sorted order contradicts the list order.
# 2. Fault a member of the special mirror; verify ZED activates the
#    smaller sufficient spare, leaving the larger spare available.
#

verify_runnable "both"

NORM1="$TEST_BASE_DIR/rotational-norm1"
NORM2="$TEST_BASE_DIR/rotational-norm2"
SPEC1="$TEST_BASE_DIR/rotational-spec1"
SPEC2="$TEST_BASE_DIR/rotational-spec2"
SPARE_SMALL="$TEST_BASE_DIR/rotational-spare-small"
SPARE_LARGE="$TEST_BASE_DIR/rotational-spare-large"

LARGE_SIZE=$((MINVDEVSIZE * 2))

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL
	rm -f $NORM1 $NORM2 $SPEC1 $SPEC2 $SPARE_SMALL $SPARE_LARGE
}

log_assert "ZED selects smallest sufficient spare for a faulted special vdev"
log_onexit cleanup

zed_events_drain

log_must truncate -s $MINVDEVSIZE $NORM1 $NORM2 $SPEC1 $SPEC2 $SPARE_SMALL
log_must truncate -s $LARGE_SIZE $SPARE_LARGE

# SPARE_LARGE is listed first so that size-preference sorting is what
# causes SPARE_SMALL to be selected, not merely list order.
log_must zpool create -f $TESTPOOL \
    mirror $NORM1 $NORM2 \
    special mirror $SPEC1 $SPEC2 \
    spare $SPARE_LARGE $SPARE_SMALL

log_must zinject -d $SPEC1 -e io -T all -f 100 $TESTPOOL
log_must zpool scrub $TESTPOOL

log_note "Wait for ZED to auto-spare the special vdev"
log_must wait_vdev_state $TESTPOOL $SPEC1 "FAULTED" 60
log_must wait_hotspare_state $TESTPOOL $SPARE_SMALL "INUSE"

# The larger spare must not have been activated.
log_must wait_hotspare_state $TESTPOOL $SPARE_LARGE "AVAIL"

log_must check_state $TESTPOOL "" "DEGRADED"

log_pass "ZED activated the smallest sufficient spare for the special vdev"
