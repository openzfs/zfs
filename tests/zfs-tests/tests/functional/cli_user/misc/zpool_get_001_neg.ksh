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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
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
# zpool get works when run as a user
#
# STRATEGY:
#
# 1. For each property, get that property
# 2. Verify the property was the same as that set in setup
#

verify_runnable "global"

log_assert "zpool get works when run as a user"

set -A props $POOL_PROPS
set -A prop_vals $POOL_VALS

while [[ $i -lt ${#args[*]} ]]
do
	PROP=${props[$i]}
	EXPECTED=${prop_vals[$i]}
	ACTUAL=$( zpool get $PROP $TESTPOOL | awk -v p=$PROP '$0 ~ p {print $1}' )
	if [ "$ACTUAL" != "$EXPECTED" ]
	then
		log_fail "Property $PROP value was $ACTUAL, expected $EXPECTED"
	fi
	i=$(( $i + 1 ))
done

log_must zpool get all $TESTPOOL

log_pass "zpool get works when run as a user"
