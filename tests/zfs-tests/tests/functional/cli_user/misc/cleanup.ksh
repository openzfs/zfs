#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
