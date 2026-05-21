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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# ZED activates a spare whose alloc_bias matches the faulted vdev's
# allocation class and does not activate a mismatched spare.
#
# STRATEGY:
# 1. Create a pool with a normal mirror, a special mirror, an unbiased
#    spare, and a special-biased spare.
# 2. Fault a member of the special mirror; verify ZED activates the
#    special-biased spare and leaves the unbiased spare available.
# 3. Clear the fault; verify the special-biased spare returns to AVAIL.
# 4. Fault a member of the normal mirror; verify ZED activates the
#    unbiased spare and leaves the special-biased spare available.
# 5. Clear the fault; verify the unbiased spare returns to AVAIL.
#

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL
	rm -f $NORMAL_DEV1 $NORMAL_DEV2 $SPECIAL_DEV1 $SPECIAL_DEV2 \
	    $SPARE_NORM $SPARE_SPEC
}

log_assert "ZED activates spares matching the faulted vdev's alloc_bias"
log_onexit cleanup

zed_events_drain

NORMAL_DEV1="$TEST_BASE_DIR/normal-dev1"
NORMAL_DEV2="$TEST_BASE_DIR/normal-dev2"
SPECIAL_DEV1="$TEST_BASE_DIR/special-dev1"
SPECIAL_DEV2="$TEST_BASE_DIR/special-dev2"
SPARE_NORM="$TEST_BASE_DIR/spare-norm"
SPARE_SPEC="$TEST_BASE_DIR/spare-spec"

log_must truncate -s $MINVDEVSIZE \
    $NORMAL_DEV1 $NORMAL_DEV2 $SPECIAL_DEV1 $SPECIAL_DEV2 \
    $SPARE_NORM $SPARE_SPEC

# 1. Create pool: normal mirror + special mirror + two spares.
log_must zpool create -f $TESTPOOL \
    mirror $NORMAL_DEV1 $NORMAL_DEV2 \
    special mirror $SPECIAL_DEV1 $SPECIAL_DEV2 \
    spare $SPARE_NORM $SPARE_SPEC
log_must zpool set alloc_bias=special $TESTPOOL $SPARE_SPEC

# Write data so the normal mirror has content for the scrub to read.
log_must dd if=/dev/urandom of=/$TESTPOOL/testfile bs=1M count=16
log_must sync_pool $TESTPOOL

# 2. Fault a member of the special mirror.
log_must zinject -d $SPECIAL_DEV1 -e io -T all -f 100 $TESTPOOL
log_must zpool scrub $TESTPOOL

log_note "Wait for ZED to activate special-biased spare"
log_must wait_vdev_state $TESTPOOL $SPECIAL_DEV1 "FAULTED" 60
log_must wait_vdev_state $TESTPOOL $SPARE_SPEC "ONLINE" 60
log_must wait_hotspare_state $TESTPOOL $SPARE_SPEC "INUSE"

# The unbiased spare must not have been activated.
log_must wait_hotspare_state $TESTPOOL $SPARE_NORM "AVAIL"

# 3. Clear the fault and verify the spare returns.
log_must zinject -c all
log_must zpool clear $TESTPOOL $SPECIAL_DEV1
log_must wait_vdev_state $TESTPOOL $SPECIAL_DEV1 "ONLINE" 60
log_must wait_hotspare_state $TESTPOOL $SPARE_SPEC "AVAIL"
log_must check_state $TESTPOOL "" "ONLINE"

# 4. Fault a member of the normal mirror.
wait_scrubbed $TESTPOOL
log_must zinject -d $NORMAL_DEV1 -e io -T all -f 100 $TESTPOOL
log_must zpool scrub $TESTPOOL

log_note "Wait for ZED to activate unbiased spare"
log_must wait_vdev_state $TESTPOOL $NORMAL_DEV1 "FAULTED" 60
log_must wait_vdev_state $TESTPOOL $SPARE_NORM "ONLINE" 60
log_must wait_hotspare_state $TESTPOOL $SPARE_NORM "INUSE"

# The special-biased spare must not have been activated.
log_must wait_hotspare_state $TESTPOOL $SPARE_SPEC "AVAIL"

# 5. Clear the fault and verify the spare returns.
log_must zinject -c all
log_must zpool clear $TESTPOOL $NORMAL_DEV1
log_must wait_vdev_state $TESTPOOL $NORMAL_DEV1 "ONLINE" 60
log_must wait_hotspare_state $TESTPOOL $SPARE_NORM "AVAIL"
log_must check_state $TESTPOOL "" "ONLINE"

log_pass "ZED spare activation respects alloc_bias"
