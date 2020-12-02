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
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
# Verify multiple dRAID spares can be used.
#
# STRATEGY:
# 1. Create a pool and fill it with data.
# 2. Engage 3 distributed spares and verify the pool
# 3. Refill the filesystem with new data
# 4. Clear the pool to online previous faulted devices and resilver
# 5. Verify the pool and its contents
#

log_assert "Verify multiple dRAID spares"

log_onexit cleanup

parity=1
spares=3
data=$(random_int_between 1 4)
children=10
draid="draid${parity}:${data}d:${children}c:${spares}s"

setup_test_env $TESTPOOL $draid $children

# Replace vdev7 -> draid1-0-0
log_must zpool offline -f $TESTPOOL $BASEDIR/vdev7
log_must zpool replace -w $TESTPOOL $BASEDIR/vdev7 draid1-0-0

# Replace vdev8 -> draid1-0-1
log_must zpool offline -f $TESTPOOL $BASEDIR/vdev8
log_must zpool replace -w $TESTPOOL $BASEDIR/vdev8 draid1-0-1

# Replace vdev9 -> draid1-0-2
log_must zpool offline -f $TESTPOOL $BASEDIR/vdev9
log_must zpool replace -w $TESTPOOL $BASEDIR/vdev9 draid1-0-2

# Verify, refill and verify the pool contents.
verify_pool $TESTPOOL
refill_test_env $TESTPOOL
verify_pool $TESTPOOL

# Bring everything back online and check for errors.
log_must zpool clear $TESTPOOL
log_must zpool wait -t resilver $TESTPOOL

log_must wait_hotspare_state $TESTPOOL draid1-0-0 "AVAIL"
log_must wait_hotspare_state $TESTPOOL draid1-0-1 "AVAIL"
log_must wait_hotspare_state $TESTPOOL draid1-0-2 "AVAIL"

log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"

log_must is_data_valid $TESTPOOL

log_pass "Verify multiple dRAID spares"
