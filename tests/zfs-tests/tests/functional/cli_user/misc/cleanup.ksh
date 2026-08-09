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

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

if poolexists $TESTPOOL.virt; then
	log_must zpool destroy $TESTPOOL.virt
fi

if poolexists v1-pool; then
	log_must zpool destroy v1-pool
fi

log_must rm -f $TEST_BASE_DIR/zfstest_datastream.dat
log_must rm -f $TEST_BASE_DIR/disk1.dat $TEST_BASE_DIR/disk2.dat \
    $TEST_BASE_DIR/disk3.dat $TEST_BASE_DIR/disk-additional.dat \
    $TEST_BASE_DIR/disk-export.dat $TEST_BASE_DIR/disk-offline.dat \
    $TEST_BASE_DIR/disk-spare1.dat $TEST_BASE_DIR/disk-spare2.dat
log_must rm -f $TEST_BASE_DIR/zfs-pool-v1.dat \
    $TEST_BASE_DIR/zfs-pool-v1.dat.bz2

default_cleanup
