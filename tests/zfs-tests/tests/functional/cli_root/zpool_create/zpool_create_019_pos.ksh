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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool create cannot create pools specifying readonly properties
#
# STRATEGY:
# 1. Attempt to create a pool, specifying each readonly property in turn
# 2. Verify the pool was not created
#

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_assert "zpool create cannot create pools specifying readonly properties"

set -A props "available" "capacity" "guid"  "health"  "size" "used"
set -A vals  "100"       "10"       "12345" "HEALTHY" "10"   "10"

typeset -i i=0;
while [ $i -lt "${#props[@]}" ]
do
        # try to set each property in the prop list with it's corresponding val
        log_mustnot zpool create -o ${props[$i]}=${vals[$i]} $TESTPOOL $DISK0
	if poolexists $TESTPOOL
	then
		log_fail "$TESTPOOL was created when setting ${props[$i]}!"
	fi
        i=$(( $i + 1))
done

log_pass "zpool create cannot create pools specifying readonly properties"
