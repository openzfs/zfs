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
