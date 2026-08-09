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
