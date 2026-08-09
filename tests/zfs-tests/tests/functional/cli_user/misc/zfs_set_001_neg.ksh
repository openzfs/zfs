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
# zfs set returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to set an array of properties on a dataset
# 2. Verify that those properties were not set and retain their original values.
#

log_assert "zfs set returns an error when run as a user"

typeset -i i=0

set -A props $PROP_NAMES
set -A prop_vals $PROP_VALS
set -A prop_new $PROP_ALTVALS

while [[ $i -lt ${#props[*]} ]]
do
	PROP=${props[$i]}
	EXPECTED=${prop_vals[$i]}
	NEW=${prop_new[$i]}
	log_mustnot zfs set $PROP=$NEW $TESTPOOL/$TESTFS/prop

	# Now verify that the above command did nothing
	ACTUAL=$(zfs get -H -o value $PROP $TESTPOOL/$TESTFS/prop)
	if [ "$ACTUAL" != "$EXPECTED" ]
	then
		log_fail "Property $PROP was set to $ACTUAL, expected $EXPECTED"
	fi
	i=$(( $i + 1 ))
done

log_pass "zfs set returns an error when run as a user"
