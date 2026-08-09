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
