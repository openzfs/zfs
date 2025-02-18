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
# Copyright (c) 2014 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_get/zpool_get_parsable.cfg

#
# DESCRIPTION:
#
# Zpool get returns parsable values for all known parsable properties
#
# STRATEGY:
# 1. For all parsable properties, verify zpool get -p returns a parsable value
#

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

typeset -i i=0

while [[ $i -lt "${#properties[@]}" ]]; do
	log_note "Checking for parsable ${properties[$i]} property"
	log_must eval "zpool get -p ${properties[$i]} $TESTPOOL >/tmp/value.$$"
	log_must grep -q "${properties[$i]}" /tmp/value.$$

	typeset v=$(awk -v p="${properties[$i]}" '$0 ~ p {print $3}' /tmp/value.$$)

	# Determine if this value is a valid number, result in return code
	log_must test -n "$v"
	expr $v + 0 >/dev/null 2>&1

	# All properties must be positive integers in order to be
	# parsable (i.e. a return code of 0 or 1 from expr above).
	# The only exception is "expandsize", which may be "-".
	if [[ ! ($? -eq 0 || $? -eq 1 || \
	    ("${properties[$i]}" = "expandsize" && "$v" = "-")) ]]; then
		log_fail "${properties[$i]} is not parsable"
	fi

	i=$(( $i + 1 ))
done

rm /tmp/value.$$
log_pass "Zpool get returns parsable values for all known parsable properties"
