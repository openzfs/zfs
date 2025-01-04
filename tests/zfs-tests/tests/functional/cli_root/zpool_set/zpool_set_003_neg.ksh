#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool set cannot set a readonly property
#
# STRATEGY:
# 1. Create a pool
# 2. Verify that we can't set readonly properties on that pool
#

verify_runnable "global"

function cleanup
{
        zpool destroy $TESTPOOL1
        rm $FILEVDEV
}

set -A props "available" "capacity" "guid"  "health"  "size" "used"
set -A vals  "100"       "10"       "12345" "HEALTHY" "10"   "10"

log_onexit cleanup

log_assert "zpool set cannot set a readonly property"

FILEVDEV="$TEST_BASE_DIR/zpool_set_003.$$.dat"
log_must mkfile $MINVDEVSIZE $FILEVDEV
log_must zpool create $TESTPOOL1 $FILEVDEV

typeset -i i=0;
while [ $i -lt "${#props[@]}" ]
do
	# try to set each property in the prop list with it's corresponding val
        log_mustnot eval "zpool set ${props[$i]}=${vals[$i]} $TESTPOOL1 \
 > /dev/null 2>&1"
        i=$(( $i + 1))
done

log_pass "zpool set cannot set a readonly property"

