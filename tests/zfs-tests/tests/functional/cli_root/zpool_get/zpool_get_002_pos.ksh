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
. $STF_SUITE/tests/functional/cli_root/zpool_get/zpool_get.cfg

#
# DESCRIPTION:
#
# zpool get all works as expected
#
# STRATEGY:
#
# 1. Using zpool get, retrieve all default values
# 2. Verify that the header is printed
# 3. Verify that we can see all the properties we expect to see
# 4. Verify that the total output contains just those properties + header.
#
# Test for those properties are expected to check whether their
# default values are sane, or whether they can be changed with zpool set.
#

function cleanup
{
	rm -f $values
}
log_onexit cleanup
log_assert "Zpool get all works as expected"

typeset -i i=0;
typeset values=$TEST_BASE_DIR/values.$$

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

log_must zpool get all $TESTPOOL
zpool get all $TESTPOOL > $values

log_note "Checking zpool get all output for a header."
log_must grep -q ^"NAME " $values


while [ $i -lt "${#properties[@]}" ]
do
	log_note "Checking for ${properties[$i]} property"
	log_must grep -q "$TESTPOOL *${properties[$i]}" $values
	i=$(( $i + 1 ))
done

# increment the counter to include the header line
i=$(( $i + 1 ))

COUNT=$(wc -l < $values)
if [ $i -ne $COUNT ]
then
	log_fail "Found zpool features not in the zpool_get test config $i/$COUNT."
fi

log_pass "Zpool get all works as expected"
