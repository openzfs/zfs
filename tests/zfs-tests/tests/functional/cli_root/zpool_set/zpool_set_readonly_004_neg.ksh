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
# Pool and dataset modifying commands fail after readonly conversion
#
# STRATEGY:
# 1. Convert a pool to readonly
# 2. Verify zpool and zfs modifying commands fail
# 3. Verify remount,rw fails
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

log_assert "Pool and dataset modifying commands fail after readonly conversion"
log_onexit cleanup

FILEVDEV="$TEST_BASE_DIR/zpool_set_readonly_004_neg.$$.dat"
FILEVDEV2="$TEST_BASE_DIR/zpool_set_readonly_004_neg_2.$$.dat"

log_must truncate -s $MINVDEVSIZE $FILEVDEV
log_must truncate -s $MINVDEVSIZE $FILEVDEV2
log_must zpool create -O canmount=off $TESTPOOL1 $FILEVDEV
log_must zfs create $TESTPOOL1/$TESTFS
log_must zfs mount -o remount,ro $TESTPOOL1/$TESTFS
log_must zpool set readonly=on $TESTPOOL1

log_mustnot zpool add $TESTPOOL1 $FILEVDEV2
log_mustnot zpool clear $TESTPOOL1
log_mustnot zpool scrub $TESTPOOL1
log_mustnot zpool reguid $TESTPOOL1
log_mustnot zfs create $TESTPOOL1/${TESTFS}_new
log_mustnot zfs set compression=on $TESTPOOL1/$TESTFS
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "on"

log_pass "Pool and dataset modifying commands fail after readonly conversion"
