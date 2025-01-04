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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zfs get works when run as a user
#
# STRATEGY:
# 1. Run zfs get with an array of different arguments
# 2. Verify for each property, we get the value that's expected
#

log_assert "zfs get works when run as a user"

typeset -i i=0

set -A props $PROP_NAMES
set -A prop_vals $PROP_VALS

while [[ $i -lt ${#props[*]} ]]
do
	PROP=${props[$i]}
	EXPECTED=${prop_vals[$i]}
	ACTUAL=$(zfs get -H -o value $PROP $TESTPOOL/$TESTFS/prop)
	if [ "$ACTUAL" != "$EXPECTED" ]
	then
		log_fail "Property $PROP value was $ACTUAL, expected $EXPECTED"
	fi
	i=$(( $i + 1 ))
done

log_pass "zfs get works when run as a user"
