#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
