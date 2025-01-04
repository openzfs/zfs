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
