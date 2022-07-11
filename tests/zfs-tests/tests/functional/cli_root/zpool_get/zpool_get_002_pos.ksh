#!/bin/ksh -p
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
