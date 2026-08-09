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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_get/zpool_get.cfg

#
# DESCRIPTION:
#
# Zpool get returns values for all known properties
#
# STRATEGY:
# 1. For all properties, verify zpool get retrieves a value
#

function cleanup
{
        rm -f $values
}

log_assert "Zpool get returns values for all known properties"
log_onexit cleanup

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

typeset -i i=0;
typeset values=$TEST_BASE_DIR/values.$$

while [ $i -lt "${#properties[@]}" ]
do
	log_note "Checking for ${properties[$i]} property"
	log_must eval "zpool get ${properties[$i]} $TESTPOOL > $values"
	log_must grep -q "${properties[$i]}" $values

	# only need to check this once.
	if [ $i -eq 0 ] && ! grep -q "^NAME " $values
	then
		log_fail "Header not seen in zpool get output"
	fi
	i=$(( $i + 1 ))
done

log_pass "Zpool get returns values for all known properties"
