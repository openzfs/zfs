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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool set/create reject unsupported readonly property values
#
# STRATEGY:
# 1. Verify zpool create -o readonly=on fails
# 2. Verify zpool set readonly=off fails on a writable pool
# 3. Convert a pool to readonly and verify readonly=off still fails
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL1/$TESTFS && \
	    ismounted $TESTPOOL1/$TESTFS && \
	    log_must zfs unmount -f $TESTPOOL1/$TESTFS
	destroy_pool $TESTPOOL1
	rm -f $FILEVDEV $FILEVDEV2
}

log_assert "zpool set/create reject unsupported readonly property values"
log_onexit cleanup

FILEVDEV="$TEST_BASE_DIR/zpool_set_readonly_002_neg.$$.dat"
FILEVDEV2="$TEST_BASE_DIR/zpool_set_readonly_002_neg_2.$$.dat"

log_must truncate -s $MINVDEVSIZE $FILEVDEV2
log_mustnot zpool create -o readonly=on $TESTPOOL1 $FILEVDEV2
log_mustnot poolexists $TESTPOOL1

log_must truncate -s $MINVDEVSIZE $FILEVDEV
log_must zpool create -O canmount=off $TESTPOOL1 $FILEVDEV
log_mustnot zpool set readonly=off $TESTPOOL1
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "off"

log_must zfs create $TESTPOOL1/$TESTFS
log_must zfs mount -o remount,ro $TESTPOOL1/$TESTFS
log_must zpool set readonly=on $TESTPOOL1
log_mustnot zpool set readonly=off $TESTPOOL1
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "on"

log_pass "zpool set/create reject unsupported readonly property values"
