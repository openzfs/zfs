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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# Device removal is possible for highly unbalanced vdevs.
#
# STRATEGY:
# 1. Create a pool with unbalanced removable devices
# 2. Remove the large top-level device
#

verify_runnable "global"

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $TEST_BASE_DIR/device-{1,2}
}

log_assert "Device removal is possible for highly unbalanced vdevs."
log_onexit cleanup

# 1. Create a pool with unbalanced removable devices
log_must truncate -s 10G $TEST_BASE_DIR/device-1
log_must truncate -s 1T $TEST_BASE_DIR/device-2
log_must default_setup_noexit "$TEST_BASE_DIR/device-1 $TEST_BASE_DIR/device-2"

log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=1M count=32

# 2. Remove the large top-level device
log_must zpool remove $TESTPOOL $TEST_BASE_DIR/device-1
log_must wait_for_removal $TESTPOOL

log_note "Capacity $(get_pool_prop capacity $TESTPOOL)"

log_pass "Device removal is possible for highly unbalanced vdevs"
