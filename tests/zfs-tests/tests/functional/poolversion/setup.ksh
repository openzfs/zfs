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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

# create a version 1 pool
log_must mkfile $MINVDEVSIZE $TEST_BASE_DIR/zpool_version_1.dat
log_must zpool create -o version=1 $TESTPOOL $TEST_BASE_DIR/zpool_version_1.dat


# create another version 1 pool
log_must mkfile $MINVDEVSIZE $TEST_BASE_DIR/zpool2_version_1.dat
log_must zpool create -o version=1 $TESTPOOL2 $TEST_BASE_DIR/zpool2_version_1.dat

log_pass
